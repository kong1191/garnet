// Copyright 2018 OpenTrustGroup. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fbl/alloc_checker.h>
#include <fbl/auto_call.h>
#include <fbl/unique_ptr.h>
#include <virtio/virtio.h>
#include <virtio/virtio_ring.h>
#include <zx/vmo.h>

#include "garnet/lib/trusty/linux_fake.h"
#include "garnet/lib/trusty/tipc_device.h"
#include "garnet/lib/trusty/tipc_msg.h"
#include "gtest/gtest.h"

#define ARRAY_SIZE(a) (sizeof(a) / sizeof(a[0]))

namespace trusty {

const int kSharedMemSize = PAGE_SIZE * 128;

const tipc_vdev_descr kTipcDescriptors[] = {
    DECLARE_TIPC_DEVICE_DESCR("dev0", 12, 16),
    DECLARE_TIPC_DEVICE_DESCR("dev1", 20, 24),
    DECLARE_TIPC_DEVICE_DESCR("dev2", 28, 32),
};

class TipcDeviceTest : public ::testing::Test {
 protected:
  virtual void SetUp() {
    // Create Shared Memory
    ASSERT_EQ(SharedMem::Create(kSharedMemSize, &shared_mem_), ZX_OK);

    // Create VirtioBus
    fbl::AllocChecker ac;
    bus_ = fbl::make_unique_checked<VirtioBus>(&ac, shared_mem_);
    ASSERT_TRUE(ac.check());

    // Create fake Linux client
    linux_ = fbl::move(LinuxFake::Create(shared_mem_));
    ASSERT_TRUE(linux_ != nullptr);

    // Create some devices on the bus
    for (uint32_t i = 0; i < ARRAY_SIZE(kTipcDescriptors); i++) {
      fbl::RefPtr<TipcDevice> dev;
      ASSERT_EQ(TipcDevice::Create(bus_.get(), &kTipcDescriptors[i], &dev),
                ZX_OK);

      // Also, create corresponding linux driver
      ASSERT_EQ(linux_->CreateDriver(dev.get()), ZX_OK);
    }
  }

  size_t ResourceTableSize(void) {
    size_t num_devices = ARRAY_SIZE(kTipcDescriptors);
    return sizeof(resource_table) +
           (sizeof(uint32_t) + sizeof(tipc_vdev_descr)) * num_devices;
  }

  fbl::RefPtr<SharedMem> shared_mem_;
  fbl::unique_ptr<VirtioBus> bus_;
  fbl::unique_ptr<LinuxFake> linux_;
  fbl::unique_ptr<TipcDriverFake> drivers_[ARRAY_SIZE(kTipcDescriptors)];
};

TEST_F(TipcDeviceTest, GetResourceTable) {
  size_t buf_size = ResourceTableSize();
  auto buf = linux_->AllocBuffer(buf_size);
  ASSERT_TRUE(buf != nullptr);
  ASSERT_EQ(bus_->GetResourceTable(buf, buf_size), ZX_OK);

  resource_table* table = reinterpret_cast<resource_table*>(buf);
  EXPECT_TRUE(table->ver == kVirtioResourceTableVersion);
  EXPECT_TRUE(table->num == ARRAY_SIZE(kTipcDescriptors));
  for (uint32_t i = 0; i < table->num; i++) {
    auto expected_desc = &kTipcDescriptors[i];
    auto expected_tx_num = expected_desc->vrings[kTipcTxQueue].num;
    auto expected_rx_num = expected_desc->vrings[kTipcRxQueue].num;
    auto expected_dev_name = expected_desc->config.dev_name;

    auto descr = rsc_entry<tipc_vdev_descr>(table, i);
    EXPECT_TRUE(descr->hdr.type == RSC_VDEV);
    EXPECT_TRUE(descr->vdev.config_len == sizeof(tipc_dev_config));
    EXPECT_TRUE(descr->vdev.num_of_vrings == kTipcNumQueues);
    EXPECT_TRUE(descr->vdev.id == kTipcVirtioDeviceId);
    EXPECT_TRUE(descr->vdev.notifyid == i);
    EXPECT_TRUE(descr->vrings[kTipcTxQueue].align == PAGE_SIZE);
    EXPECT_TRUE(descr->vrings[kTipcTxQueue].num == expected_tx_num);
    EXPECT_TRUE(descr->vrings[kTipcTxQueue].notifyid == 1u);
    EXPECT_TRUE(descr->vrings[kTipcRxQueue].align == PAGE_SIZE);
    EXPECT_TRUE(descr->vrings[kTipcRxQueue].num == expected_rx_num);
    EXPECT_TRUE(descr->vrings[kTipcRxQueue].notifyid == 2u);
    EXPECT_TRUE(descr->config.msg_buf_max_size == PAGE_SIZE);
    EXPECT_TRUE(descr->config.msg_buf_alignment == PAGE_SIZE);
    EXPECT_STREQ(descr->config.dev_name, expected_dev_name);
  }
}

TEST_F(TipcDeviceTest, GetResourceTableFailed) {
  auto buf = shared_mem_->as<uint8_t>(0);
  size_t buf_size = shared_mem_->size();

  // pass smaller buffer not able to fit resource table should fail
  ASSERT_EQ(bus_->GetResourceTable(buf, ResourceTableSize() - 1),
            ZX_ERR_NO_MEMORY);

  // pass buffer outside the shared memory region should fail
  ASSERT_EQ(bus_->GetResourceTable(buf - 1, buf_size), ZX_ERR_OUT_OF_RANGE);
  ASSERT_EQ(bus_->GetResourceTable(buf + buf_size - 1, buf_size),
            ZX_ERR_OUT_OF_RANGE);
}

TEST_F(TipcDeviceTest, VirtioBusOnLine) {
  size_t buf_size = ResourceTableSize();
  auto buf = linux_->AllocBuffer(buf_size);
  ASSERT_TRUE(buf != nullptr);
  ASSERT_EQ(bus_->GetResourceTable(buf, buf_size), ZX_OK);

  auto table = reinterpret_cast<resource_table*>(buf);
  ASSERT_EQ(linux_->HandleResourceTable(table), ZX_OK);

  // Allocate buffer for online message
  for (const auto& driver : linux_->drivers()) {
    size_t msg_size = sizeof(tipc_hdr) + sizeof(tipc_ctrl_msg_hdr);
    auto msg_buf = linux_->AllocBuffer(msg_size);
    ASSERT_TRUE(msg_buf != nullptr);

    ASSERT_EQ(driver->AddRxBuffer(msg_buf, msg_size), ZX_OK);
  }

  // Sent the vring shared memroy to VirtioBus, and notify it to start service
  ASSERT_EQ(bus_->Start(buf, buf_size), ZX_OK);

  // Verify the online message
  for (const auto& driver : linux_->drivers()) {
    virtio_desc_t desc;
    EXPECT_EQ(driver->GetRxBuffer(&desc), ZX_OK);
    EXPECT_TRUE(desc.len == sizeof(tipc_hdr) + sizeof(tipc_ctrl_msg_hdr));
    EXPECT_FALSE(desc.has_next);

    auto hdr = reinterpret_cast<tipc_hdr*>(desc.addr);
    EXPECT_EQ(hdr->src, kTipcCtrlAddress);
    EXPECT_EQ(hdr->dst, kTipcCtrlAddress);
    EXPECT_EQ(hdr->len, sizeof(tipc_ctrl_msg_hdr));

    auto ctrl_hdr = reinterpret_cast<tipc_ctrl_msg_hdr*>(hdr + 1);
    EXPECT_EQ(ctrl_hdr->type, CtrlMessage::GO_ONLINE);
    EXPECT_EQ(ctrl_hdr->body_len, 0u);
  }
}

}  // namespace trusty
