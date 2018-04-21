// Copyright 2018 Open Trust Group.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string.h>
#include <lib/async-loop/cpp/loop.h>
#include <zx/channel.h>

#include "garnet/bin/gzos/smc_service/smc_service.h"
#include "garnet/bin/gzos/smc_service/trusty_smc.h"
#include "garnet/lib/trusty/tipc_device.h"
#include "lib/fxl/threading/thread.h"

#include "gtest/gtest.h"

// clang-format off
/* Normal memory */
#define NS_MAIR_NORMAL_CACHED_WB_RWA       0xFF /* inner and outer write back read/write allocate */
#define NS_MAIR_NORMAL_CACHED_WT_RA        0xAA /* inner and outer write through read allocate */
#define NS_MAIR_NORMAL_CACHED_WB_RA        0xEE /* inner and outer write back, read allocate */
#define NS_MAIR_NORMAL_UNCACHED            0x44 /* uncached */

/* shareable attributes */
#define NS_NON_SHAREABLE                   0x0
#define NS_OUTER_SHAREABLE                 0x2
#define NS_INNER_SHAREABLE                 0x3
// clang-format on

namespace smc_service {

class SmcServiceTest : public testing::Test {
 protected:
  virtual void SetUp() {
    smc_service_ = SmcService::GetInstance();
    ASSERT_TRUE(smc_service_ != nullptr);
  }

  async::Loop loop_;
  SmcService* smc_service_;
};

class SmcTestEntity : public SmcEntity {
 public:
  SmcTestEntity(smc32_args_t* args) : smc_args_(args) {}
  zx_status_t Init() { return ZX_OK; }
  long InvokeSmcFunction(smc32_args_t* args) {
    memcpy(smc_args_, args, sizeof(smc32_args_t));
    return 0;
  }

 private:
  smc32_args_t* smc_args_;
};

TEST_F(SmcServiceTest, SmcEntityTest) {
  smc32_args_t smc_args = {};
  smc_service_->AddSmcEntity(SMC_ENTITY_TRUSTED_OS,
                             new SmcTestEntity(&smc_args));
  smc_service_->Start(loop_.async());

  /* issue a test smc call */
  fxl::Thread smc_test_thread([&] {
    long smc_ret = -1;
    smc32_args_t expect_smc_args = {
        .smc_nr = SMC_SC_VIRTIO_START,
        .params = {0x123U, 0x456U, 0x789U},
    };
    zx_status_t st = zx_smc_call_test(smc_service_->GetHandle(),
                                      &expect_smc_args,
                                      &smc_ret);
    ASSERT_EQ(st, ZX_OK);
    EXPECT_EQ(smc_args.smc_nr, expect_smc_args.smc_nr);
    EXPECT_EQ(smc_args.params[0], expect_smc_args.params[0]);
    EXPECT_EQ(smc_args.params[1], expect_smc_args.params[1]);
    EXPECT_EQ(smc_args.params[2], expect_smc_args.params[2]);
    EXPECT_EQ(smc_ret, 0);
  });
  EXPECT_TRUE(smc_test_thread.Run());

  /* receive smc call once and leave the loop */
  loop_.Run(zx::time::infinite(), true);

  EXPECT_TRUE(smc_test_thread.Join());
}

class TrustySmcTest : public SmcServiceTest {
 protected:
  virtual void SetUp() {
    SmcServiceTest::SetUp();

    zx_status_t status = zx::channel::create(0, &ch1_, &ch2_);
    ASSERT_EQ(status, ZX_OK);

    fbl::RefPtr<SharedMem> shm = smc_service_->GetSharedMem();
    fbl::AllocChecker ac;
    entity_ = fbl::make_unique_checked<TrustySmcEntity>(&ac, loop_.async(),
                                                        ch1_.get(), shm);
    ASSERT_TRUE(ac.check());

    ASSERT_EQ(entity_->Init(), ZX_OK);

    ns_buf_size_ = PAGE_SIZE;
    ns_buf_ = shm->ptr(0, ns_buf_size_);
    ns_buf_pa_ = shm->VirtToPhys(ns_buf_, ns_buf_size_);
  }

  uint64_t MemAttr(uint64_t mair,
                   uint64_t shareable,
                   bool ap_user = false,
                   bool ap_ro = false) {
    uint64_t ap = ap_user ? 1ULL : 0ULL;

    if (ap_ro) {
      ap |= 2ULL;
    }
    return ns_buf_pa_ | (mair << 48) | (shareable << 8) | (ap << 6);
  }

  long InvokeSmc(uint64_t mem_attr) {
    smc32_args_t smc_args = {
        .smc_nr = SMC_SC_VIRTIO_GET_DESCR,
        .params = {static_cast<uint32_t>(mem_attr & 0xffffffff),
                   static_cast<uint32_t>((mem_attr >> 32) & 0xffffffff),
                   static_cast<uint32_t>(ns_buf_size_)},
    };

    return entity_->InvokeSmcFunction(&smc_args);
  }

  zx::channel ch1_, ch2_;
  fbl::unique_ptr<TrustySmcEntity> entity_;
  void* ns_buf_;
  size_t ns_buf_size_;
  uintptr_t ns_buf_pa_;
};

TEST_F(TrustySmcTest, VirtioGetDescriptorTest) {
  uint64_t mem_attr = MemAttr(NS_MAIR_NORMAL_CACHED_WB_RWA, NS_INNER_SHAREABLE);
  ASSERT_GE((size_t)InvokeSmc(mem_attr), sizeof(resource_table));

  resource_table* table = reinterpret_cast<resource_table*>(ns_buf_);
  EXPECT_EQ(table->ver, trusty::kVirtioResourceTableVersion);
  EXPECT_EQ(table->num, 1u);

  auto descr = trusty::rsc_entry<trusty::tipc_vdev_descr>(table, 0);
  EXPECT_EQ(descr->hdr.type, RSC_VDEV);
  EXPECT_EQ(descr->vdev.config_len, sizeof(trusty::tipc_dev_config));
  EXPECT_EQ(descr->vdev.num_of_vrings, trusty::kTipcNumQueues);
  EXPECT_EQ(descr->vdev.id, trusty::kTipcVirtioDeviceId);
  EXPECT_EQ(descr->vrings[trusty::kTipcTxQueue].num, 32u);
  EXPECT_EQ(descr->vrings[trusty::kTipcTxQueue].notifyid, 1u);
  EXPECT_EQ(descr->vrings[trusty::kTipcRxQueue].num, 32u);
  EXPECT_EQ(descr->vrings[trusty::kTipcRxQueue].notifyid, 2u);
  EXPECT_STREQ(descr->config.dev_name, "dev0");
}

TEST_F(TrustySmcTest, InvalidSmcTest) {
  smc32_args_t smc_args = {
      .smc_nr = 0xabc,
  };
  ASSERT_EQ(entity_->InvokeSmcFunction(&smc_args), SM_ERR_NOT_SUPPORTED);
}

TEST_F(TrustySmcTest, InvalidMemAttrTest) {
  /* invalid mair */
  uint64_t mem_attr = MemAttr(NS_MAIR_NORMAL_UNCACHED + 1, NS_INNER_SHAREABLE);
  ASSERT_EQ(InvokeSmc(mem_attr), SM_ERR_NOT_SUPPORTED);

  /* cache attribute and cache policy mismatch */
  mem_attr = MemAttr(NS_MAIR_NORMAL_UNCACHED, NS_INNER_SHAREABLE);
  ASSERT_EQ(InvokeSmc(mem_attr), SM_ERR_INVALID_PARAMETERS);

  /* invalid shareable attribute */
  mem_attr = MemAttr(NS_MAIR_NORMAL_CACHED_WB_RWA, NS_NON_SHAREABLE);
  ASSERT_EQ(InvokeSmc(mem_attr), SM_ERR_NOT_SUPPORTED);

  /* ns user page is not allow */
  mem_attr =
      MemAttr(NS_MAIR_NORMAL_CACHED_WB_RWA, NS_INNER_SHAREABLE, true, false);
  ASSERT_EQ(InvokeSmc(mem_attr), SM_ERR_INVALID_PARAMETERS);

  /* ns ro page is not allow */
  mem_attr =
      MemAttr(NS_MAIR_NORMAL_CACHED_WB_RWA, NS_INNER_SHAREABLE, false, true);
  ASSERT_EQ(InvokeSmc(mem_attr), SM_ERR_INVALID_PARAMETERS);
}

TEST_F(TrustySmcTest, InvalidNsBufTest) {
  uint64_t mem_attr = MemAttr(NS_MAIR_NORMAL_CACHED_WB_RWA, NS_INNER_SHAREABLE);

  /* adjust ns buf phys addr to an invalid address by minus 1 */
  mem_attr -= (1ULL << 12);
  ASSERT_EQ(InvokeSmc(mem_attr), SM_ERR_NOT_ALLOWED);
}

} // namespace smc_service
