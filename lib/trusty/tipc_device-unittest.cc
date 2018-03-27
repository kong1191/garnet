// Copyright 2018 OpenTrustGroup. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fbl/alloc_checker.h>
#include <fbl/auto_call.h>
#include <fbl/unique_ptr.h>
#include <lib/async/cpp/loop.h>
#include <virtio/virtio.h>
#include <virtio/virtio_ring.h>
#include <zx/channel.h>
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

class ResourceTableTest : public ::testing::Test {
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
      fbl::RefPtr<TipcDevice> dev =
          fbl::AdoptRef(new TipcDevice(kTipcDescriptors[i]));
      ASSERT_TRUE(dev != nullptr);

      ASSERT_EQ(bus_->AddDevice(dev), ZX_OK);
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
};

TEST_F(ResourceTableTest, GetResourceTable) {
  size_t buf_size = ResourceTableSize();
  auto buf = linux_->AllocBuffer(buf_size);
  ASSERT_TRUE(buf != nullptr);
  ASSERT_EQ(bus_->GetResourceTable(buf, &buf_size), ZX_OK);

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

TEST_F(ResourceTableTest, FailedToGetResourceTable) {
  auto buf = shared_mem_->as<uint8_t>(0);

  // pass smaller buffer not able to fit resource table should fail
  size_t buf_size = ResourceTableSize() - 1;
  ASSERT_EQ(bus_->GetResourceTable(buf, &buf_size), ZX_ERR_NO_MEMORY);

  // pass buffer outside the shared memory region should fail
  buf_size = shared_mem_->size();
  ASSERT_EQ(bus_->GetResourceTable(buf - 1, &buf_size), ZX_ERR_OUT_OF_RANGE);
  ASSERT_EQ(bus_->GetResourceTable(buf + buf_size - 1, &buf_size),
            ZX_ERR_OUT_OF_RANGE);
}

class TransactionTest : public ::testing::Test {
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

    // Create device on the bus
    tipc_dev_ = fbl::AdoptRef(new TipcDevice(kTipcDescriptors[0]));
    ASSERT_TRUE(tipc_dev_ != nullptr);
    ASSERT_EQ(bus_->AddDevice(tipc_dev_), ZX_OK);

    // Also, create corresponding linux tipc driver
    ASSERT_EQ(linux_->CreateDriver(tipc_dev_.get()), ZX_OK);

    // Make virtio bus online
    VirtioBusOnline();
  }

  void VirtioBusOnline() {
    const auto& tipc_driver = linux_->drivers()[0];

    size_t buf_size = PAGE_SIZE;
    auto buf = linux_->AllocBuffer(buf_size);
    ASSERT_TRUE(buf != nullptr);
    ASSERT_EQ(bus_->GetResourceTable(buf, &buf_size), ZX_OK);

    auto table = reinterpret_cast<resource_table*>(buf);
    ASSERT_EQ(linux_->HandleResourceTable(table), ZX_OK);

    // Allocate buffer for online message
    size_t msg_size = sizeof(tipc_hdr) + sizeof(tipc_ctrl_msg_hdr);
    auto msg_buf = linux_->AllocBuffer(msg_size);
    ASSERT_TRUE(msg_buf != nullptr);
    ASSERT_EQ(tipc_driver->rx_queue()
                  .BuildDescriptor()
                  .AppendWriteable(msg_buf, msg_size)
                  .Build(),
              ZX_OK);

    // Sent the processed resource table back to VirtioBus, and notify
    // it to start service. Each Tipc device will send back a control
    // message to tipc driver after it is ready to online.
    ASSERT_EQ(bus_->Start(buf, buf_size), ZX_OK);

    // Verify the online message
    virtio_desc_t desc;
    volatile vring_used_elem* used = tipc_driver->rx_queue().ReadFromUsed();
    ASSERT_TRUE(used != nullptr);
    EXPECT_EQ(tipc_driver->rx_queue().queue()->ReadDesc(used->id, &desc),
              ZX_OK);
    EXPECT_TRUE(used->len == sizeof(tipc_hdr) + sizeof(tipc_ctrl_msg_hdr));
    EXPECT_FALSE(desc.has_next);

    auto hdr = reinterpret_cast<tipc_hdr*>(desc.addr);
    EXPECT_EQ(hdr->src, kTipcCtrlAddress);
    EXPECT_EQ(hdr->dst, kTipcCtrlAddress);
    EXPECT_EQ(hdr->len, sizeof(tipc_ctrl_msg_hdr));

    auto ctrl_hdr = reinterpret_cast<tipc_ctrl_msg_hdr*>(hdr + 1);
    EXPECT_EQ(ctrl_hdr->type, CtrlMessage::GO_ONLINE);
    EXPECT_EQ(ctrl_hdr->body_len, 0u);
  }

  fbl::RefPtr<SharedMem> shared_mem_;
  fbl::RefPtr<TipcDevice> tipc_dev_;
  fbl::unique_ptr<VirtioBus> bus_;
  fbl::unique_ptr<LinuxFake> linux_;
};

TEST_F(TransactionTest, ReceiveTest) {
  constexpr int kNumRxBuffers = 2;
  const auto& tipc_driver = linux_->drivers()[0];

  // allocate some RX buffers
  for (int i = 0; i < kNumRxBuffers; i++) {
    size_t msg_size = PAGE_SIZE;
    auto msg_buf = linux_->AllocBuffer(msg_size);
    ASSERT_TRUE(msg_buf != nullptr);

    ASSERT_EQ(tipc_driver->rx_queue()
                  .BuildDescriptor()
                  .AppendWriteable(msg_buf, msg_size)
                  .Build(),
              ZX_OK);
  }

  const char msg1[] = "This is the first message";
  const char msg2[] = "This is the second message";
  async::Loop loop;

  // Create channel and connected with tipc device
  zx::channel ch0, ch1;
  ASSERT_EQ(zx::channel::create(0, &ch0, &ch1), ZX_OK);
  ASSERT_EQ(tipc_dev_->Connect(loop.async(), fbl::move(ch1)), ZX_OK);

  // Write some messages to the tipc device
  ASSERT_EQ(ch0.write(0, msg1, sizeof(msg1), NULL, 0), ZX_OK);
  ASSERT_EQ(ch0.write(0, msg2, sizeof(msg2), NULL, 0), ZX_OK);

  loop.RunUntilIdle();

  // Verify the message from linux side
  virtio_desc_t desc;
  volatile vring_used_elem* used = tipc_driver->rx_queue().ReadFromUsed();
  ASSERT_TRUE(used != nullptr);
  EXPECT_EQ(tipc_driver->rx_queue().queue()->ReadDesc(used->id, &desc), ZX_OK);
  EXPECT_TRUE(used->len == sizeof(msg1));
  EXPECT_STREQ(reinterpret_cast<const char*>(desc.addr), msg1);

  used = tipc_driver->rx_queue().ReadFromUsed();
  ASSERT_TRUE(used != nullptr);
  EXPECT_EQ(tipc_driver->rx_queue().queue()->ReadDesc(used->id, &desc), ZX_OK);
  EXPECT_TRUE(used->len == sizeof(msg2));
  EXPECT_STREQ(reinterpret_cast<const char*>(desc.addr), msg2);
}

TEST_F(TransactionTest, SendTest) {
  const auto& tipc_driver = linux_->drivers()[0];

  const char msg1[] = "This is the first message";
  const char msg2[] = "This is the second message";
  async::Loop loop;

  // Create channel and connected with tipc device
  zx::channel ch0, ch1;
  ASSERT_EQ(zx::channel::create(0, &ch0, &ch1), ZX_OK);
  ASSERT_EQ(tipc_dev_->Connect(loop.async(), fbl::move(ch1)), ZX_OK);

  // Send some buffers from linux side
  auto buf = linux_->AllocBuffer(sizeof(msg1));
  ASSERT_TRUE(buf != nullptr);
  memcpy(buf, msg1, sizeof(msg1));
  ASSERT_EQ(tipc_driver->tx_queue()
                .BuildDescriptor()
                .AppendReadable(buf, sizeof(msg1))
                .Build(),
            ZX_OK);

  buf = linux_->AllocBuffer(sizeof(msg2));
  ASSERT_TRUE(buf != nullptr);
  memcpy(buf, msg2, sizeof(msg2));
  ASSERT_EQ(tipc_driver->tx_queue()
                .BuildDescriptor()
                .AppendReadable(buf, sizeof(msg2))
                .Build(),
            ZX_OK);

  loop.RunUntilIdle();

  // Read the message from tipc device and verify it
  char msg_buf[256];
  uint32_t byte_read;
  ASSERT_EQ(ch0.read(0, msg_buf, sizeof(msg_buf), &byte_read, NULL, 0, NULL),
            ZX_OK);
  EXPECT_EQ(sizeof(msg1), byte_read);
  EXPECT_STREQ(msg_buf, msg1);

  ASSERT_EQ(ch0.read(0, msg_buf, sizeof(msg_buf), &byte_read, NULL, 0, NULL),
            ZX_OK);
  EXPECT_EQ(sizeof(msg2), byte_read);
  EXPECT_STREQ(msg_buf, msg2);
}

}  // namespace trusty
