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
#include "gtest/gtest.h"

#define ARRAY_SIZE(a) (sizeof(a) / sizeof(a[0]))

namespace trusty {

const int kSharedMemSize = PAGE_SIZE * 128;

const TipcDevice::Config kTipcDevConfigs[] = {
    {
        "dev0",  // dev_name
        12,      // tx_buf_size
        16       // rx_buf_size
    },
    {
        "dev1",  // dev_name
        20,      // tx_buf_size
        24       // rx_buf_size
    },
    {
        "dev2",  // dev_name
        28,      // tx_buf_size
        32       // rx_buf_size
    },
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
    for (uint32_t i = 0; i < ARRAY_SIZE(kTipcDevConfigs); i++) {
      fbl::RefPtr<TipcDevice> dev;
      ASSERT_EQ(TipcDevice::Create(bus_.get(), &kTipcDevConfigs[i], &dev),
                ZX_OK);

      // Also, create the corresponding fake driver
      drivers_[i] = fbl::make_unique_checked<TipcDriverFake>(&ac, dev.get(),
                                                             linux_.get());
      ASSERT_TRUE(ac.check());
    }
  }

  size_t ResourceTableSize(void) {
    size_t num_devices = ARRAY_SIZE(kTipcDevConfigs);
    return sizeof(resource_table) +
           (sizeof(uint32_t) + sizeof(tipc_vdev_descr)) * num_devices;
  }

  fbl::RefPtr<SharedMem> shared_mem_;
  fbl::unique_ptr<VirtioBus> bus_;
  fbl::unique_ptr<LinuxFake> linux_;
  fbl::unique_ptr<TipcDriverFake> drivers_[ARRAY_SIZE(kTipcDevConfigs)];
};

TEST_F(TipcDeviceTest, GetResourceTable) {
  size_t buf_size = ResourceTableSize();
  void* buf = linux_->AllocBuffer(buf_size);
  ASSERT_TRUE(buf != nullptr);
  ASSERT_EQ(bus_->GetResourceTable(buf, buf_size), ZX_OK);

  auto free_buf =
      fbl::MakeAutoCall([this, buf]() { this->linux_->FreeBuffer(buf); });

  resource_table* table = reinterpret_cast<resource_table*>(buf);
  EXPECT_TRUE(table->ver == kVirtioRscVersion);
  EXPECT_TRUE(table->num == ARRAY_SIZE(kTipcDevConfigs));
  for (uint32_t i = 0; i < table->num; i++) {
    tipc_vdev_descr* descr = reinterpret_cast<tipc_vdev_descr*>(
        reinterpret_cast<uint8_t*>(table) + table->offset[i]);

    EXPECT_TRUE(descr->hdr.type == RSC_VDEV);
    EXPECT_TRUE(descr->vdev.config_len == sizeof(tipc_dev_config));
    EXPECT_TRUE(descr->vdev.num_of_vrings == kNumQueues);
    EXPECT_TRUE(descr->vdev.id == kVirtioTipcId);
    EXPECT_TRUE(descr->vdev.notifyid == i);
    EXPECT_TRUE(descr->vrings[0].align == PAGE_SIZE);
    EXPECT_TRUE(descr->vrings[0].num == kTipcDevConfigs[i].tx_buf_size);
    EXPECT_TRUE(descr->vrings[0].notifyid == 1u);
    EXPECT_TRUE(descr->vrings[1].align == PAGE_SIZE);
    EXPECT_TRUE(descr->vrings[1].num == kTipcDevConfigs[i].rx_buf_size);
    EXPECT_TRUE(descr->vrings[1].notifyid == 2u);
    EXPECT_TRUE(descr->config.msg_buf_max_size == PAGE_SIZE);
    EXPECT_TRUE(descr->config.msg_buf_alignment == PAGE_SIZE);
    EXPECT_STREQ(descr->config.dev_name, kTipcDevConfigs[i].dev_name);
  }
}

TEST_F(TipcDeviceTest, GetResourceTableFailed) {
  uint8_t* buf = shared_mem_->as<uint8_t>(0);
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
  void* buf = linux_->AllocBuffer(buf_size);
  ASSERT_TRUE(buf != nullptr);
  ASSERT_EQ(bus_->GetResourceTable(buf, buf_size), ZX_OK);

  // Fake Linux client allocates shared memory for vrings by probing the vdev
  // descriptor provided by VirtioBus
  auto table = reinterpret_cast<resource_table*>(buf);
  for (uint32_t i = 0; i < table->num; i++) {
    tipc_vdev_descr* vdev_descr = reinterpret_cast<tipc_vdev_descr*>(
        reinterpret_cast<uint8_t*>(table) + table->offset[i]);

    ASSERT_EQ(drivers_[i]->Probe(&vdev_descr->vdev), ZX_OK);
  }

  // Sent the vring shared memroy to VirtioBus, and notify it to start service
  ASSERT_EQ(bus_->Start(buf, buf_size), ZX_OK);
}

}  // namespace trusty
