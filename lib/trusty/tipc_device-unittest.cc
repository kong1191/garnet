// Copyright 2018 OpenTrustGroup. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/io-buffer.h>
#include <fbl/alloc_checker.h>
#include <fbl/unique_ptr.h>
#include <zx/vmo.h>

#include "garnet/lib/trusty/tipc_device.h"
#include "gtest/gtest.h"

#define ARRAY_SIZE(a) (sizeof(a) / sizeof(a[0]))

namespace trusty {

// TODO(sy): keep this single PAGE until we find a way to get physically
// contiguous vmo
const int kSharedMemSize = PAGE_SIZE;

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
    zx::vmo vmo;
    ASSERT_EQ(zx::vmo::create(kSharedMemSize, 0, &vmo), ZX_OK);
    ASSERT_EQ(SharedMem::Create(fbl::move(vmo), &shared_mem_), ZX_OK);

    // Create VirtioBus
    fbl::AllocChecker ac;
    bus_ = fbl::make_unique_checked<VirtioBus>(&ac, shared_mem_);
    ASSERT_TRUE(ac.check());

    // Create some devices on the bus
    for (uint32_t i = 0; i < ARRAY_SIZE(kTipcDevConfigs); i++)
      ASSERT_EQ(TipcDevice::Create(bus_.get(), &kTipcDevConfigs[i]), ZX_OK);
  }

  SharedMemPtr shared_mem_;

  fbl::unique_ptr<VirtioBus> bus_;
};

TEST_F(TipcDeviceTest, GetResourceTable) {
  uint8_t* buf = shared_mem_->as<uint8_t>(0);
  size_t buf_size = shared_mem_->size();
  ASSERT_EQ(bus_->GetResourceTable(buf, buf_size), ZX_OK);

  resource_table* table = reinterpret_cast<resource_table*>(buf);
  EXPECT_TRUE(table->ver == kVirtioRscVersion);
  EXPECT_TRUE(table->num == ARRAY_SIZE(kTipcDevConfigs));
  for (uint32_t i = 0; i < table->num; i++) {
    tipc_vdev_descr* descr =
        reinterpret_cast<tipc_vdev_descr*>(table + table->offset[i]);
    EXPECT_TRUE(descr->hdr.type == RSC_VDEV);
    EXPECT_TRUE(descr->vdev.config_len == sizeof(tipc_dev_config));
    EXPECT_TRUE(descr->vdev.num_of_vrings == kNumQueues);
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

  // pass small buffer should fail
  ASSERT_EQ(bus_->GetResourceTable(buf, 1), ZX_ERR_NO_MEMORY);

  // pass buffer outside the shared memory should fail
  ASSERT_EQ(bus_->GetResourceTable(buf - 1, buf_size), ZX_ERR_OUT_OF_RANGE);
  ASSERT_EQ(bus_->GetResourceTable(buf + buf_size - 1, buf_size),
            ZX_ERR_OUT_OF_RANGE);
}

}  // namespace trusty
