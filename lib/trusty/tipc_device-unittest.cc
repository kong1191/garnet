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

const struct {
  const char* dev_name;
  uint32_t notify_id;
  size_t tx_buf_size;
  size_t rx_buf_size;
} kTipcDevices[] = {
    {"dev0", 0, 12, 16},
    {"dev1", 1, 20, 24},
    {"dev2", 2, 28, 32},
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
    TipcDeviceBuilder builder;
    for (uint32_t i = 0; i < ARRAY_SIZE(kTipcDevices); i++) {
      fbl::unique_ptr<TipcDevice> dev;
      zx_status_t status = builder.SetNotifyId(kTipcDevices[i].notify_id)
                               .SetTxBufSize(kTipcDevices[i].tx_buf_size)
                               .SetRxBufSize(kTipcDevices[i].rx_buf_size)
                               .SetDevName(kTipcDevices[i].dev_name)
                               .Build(&dev);
      ASSERT_EQ(status, ZX_OK);

      status = bus_->AddDevice(fbl::move(dev));
      ASSERT_EQ(status, ZX_OK);
    }
  }

  void CheckResourceTable(resource_table* table) {
    EXPECT_EQ(table->ver, kVirtioRscVersion);
    for (uint32_t i = 0; i < table->num; i++) {
      tipc_vdev_descr* descr =
          reinterpret_cast<tipc_vdev_descr*>(table + table->offset[i]);
      EXPECT_EQ(descr->hdr.type, RSC_VDEV);
      EXPECT_EQ(descr->vdev.config_len, sizeof(tipc_dev_config));
      EXPECT_EQ(descr->vdev.num_of_vrings, kNumQueues);
      EXPECT_EQ(descr->vdev.notifyid, kTipcDevices[i].notify_id);
      EXPECT_TRUE(descr->vrings[0].align == PAGE_SIZE);
      EXPECT_EQ(descr->vrings[0].num, kTipcDevices[i].tx_buf_size);
      EXPECT_EQ(descr->vrings[0].notifyid, 1u);
      EXPECT_TRUE(descr->vrings[1].align == PAGE_SIZE);
      EXPECT_EQ(descr->vrings[1].num, kTipcDevices[i].rx_buf_size);
      EXPECT_EQ(descr->vrings[1].notifyid, 2u);
      EXPECT_TRUE(descr->config.msg_buf_max_size == PAGE_SIZE);
      EXPECT_TRUE(descr->config.msg_buf_alignment == PAGE_SIZE);
      EXPECT_STREQ(descr->config.dev_name, kTipcDevices[i].dev_name);
    }
  }

  SharedMemPtr shared_mem_;

  fbl::unique_ptr<VirtioBus> bus_;
};

TEST_F(TipcDeviceTest, ResourceTable) {
  uint8_t* buf = shared_mem_->as<uint8_t>(0);
  size_t buf_size = shared_mem_->size();
  ASSERT_EQ(bus_->GetResourceTable(buf, buf_size), ZX_OK);

  CheckResourceTable(reinterpret_cast<resource_table*>(buf));
}

}  // namespace trusty
