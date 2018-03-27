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

const tipc_vdev_descr kTipcDescrs[] = {
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
    for (uint32_t i = 0; i < ARRAY_SIZE(kTipcDescrs); i++) {
      fbl::RefPtr<TipcDevice> dev;
      ASSERT_EQ(TipcDevice::Create(bus_.get(), &kTipcDescrs[i], &dev), ZX_OK);

      // Also, create the corresponding fake driver
      drivers_[i] = fbl::make_unique_checked<TipcDriverFake>(&ac, dev.get(),
                                                             linux_.get());
      ASSERT_TRUE(ac.check());
    }
  }

  size_t ResourceTableSize(void) {
    size_t num_devices = ARRAY_SIZE(kTipcDescrs);
    return sizeof(resource_table) +
           (sizeof(uint32_t) + sizeof(tipc_vdev_descr)) * num_devices;
  }

  fbl::RefPtr<SharedMem> shared_mem_;
  fbl::unique_ptr<VirtioBus> bus_;
  fbl::unique_ptr<LinuxFake> linux_;
  fbl::unique_ptr<TipcDriverFake> drivers_[ARRAY_SIZE(kTipcDescrs)];
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
  EXPECT_TRUE(table->num == ARRAY_SIZE(kTipcDescrs));
  for (uint32_t i = 0; i < table->num; i++) {
    tipc_vdev_descr* descr = reinterpret_cast<tipc_vdev_descr*>(
        reinterpret_cast<uint8_t*>(table) + table->offset[i]);

    EXPECT_TRUE(descr->hdr.type == RSC_VDEV);
    EXPECT_TRUE(descr->vdev.config_len == sizeof(tipc_dev_config));
    EXPECT_TRUE(descr->vdev.num_of_vrings == kNumQueues);
    EXPECT_TRUE(descr->vdev.id == kVirtioTipcId);
    EXPECT_TRUE(descr->vdev.notifyid == i);
    EXPECT_TRUE(descr->vrings[kTipcVqTx].align == PAGE_SIZE);
    EXPECT_TRUE(descr->vrings[kTipcVqTx].num ==
                kTipcDescrs[i].vrings[kTipcVqTx].num);
    EXPECT_TRUE(descr->vrings[kTipcVqTx].notifyid == 1u);
    EXPECT_TRUE(descr->vrings[kTipcVqRx].align == PAGE_SIZE);
    EXPECT_TRUE(descr->vrings[kTipcVqRx].num ==
                kTipcDescrs[i].vrings[kTipcVqRx].num);
    EXPECT_TRUE(descr->vrings[kTipcVqRx].notifyid == 2u);
    EXPECT_TRUE(descr->config.msg_buf_max_size == PAGE_SIZE);
    EXPECT_TRUE(descr->config.msg_buf_alignment == PAGE_SIZE);
    EXPECT_STREQ(descr->config.dev_name, kTipcDescrs[i].config.dev_name);
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
    tipc_vdev_descr* descr = reinterpret_cast<tipc_vdev_descr*>(
        reinterpret_cast<uint8_t*>(table) + table->offset[i]);

    ASSERT_EQ(drivers_[i]->Probe(&descr->vdev), ZX_OK);
  }

  // Sent the vring shared memroy to VirtioBus, and notify it to start service
  ASSERT_EQ(bus_->Start(buf, buf_size), ZX_OK);
}

}  // namespace trusty
