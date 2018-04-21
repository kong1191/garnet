// Copyright 2018 Open Trust Group.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string.h>
#include <lib/async-loop/cpp/loop.h>

#include "garnet/bin/gzos/smc_service/smc_service.h"
#include "lib/fxl/threading/thread.h"

#include "gtest/gtest.h"

namespace smc_service {

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

TEST(SmcServiceTest, FakeSmc) {
  zx_handle_t smc_handle = ZX_HANDLE_INVALID;
  zx_handle_t vmo_handle = ZX_HANDLE_INVALID;
  zx_info_smc_t smc_info = {};

  zx_status_t status = zx_smc_create(0, &smc_info, sizeof(zx_info_smc_t),
                                     &smc_handle, &vmo_handle);
  ASSERT_EQ(status, ZX_OK);

  SmcService* service = SmcService::Create(smc_handle, vmo_handle, smc_info);
  ASSERT_TRUE(service != nullptr);

  async_loop_config_t config = {
      .make_default_for_current_thread = true,
  };
  async::Loop loop(&config);

  smc32_args_t smc_args = {};
  service->AddSmcEntity(SMC_ENTITY_TRUSTED_OS, new SmcTestEntity(&smc_args));
  service->Start(loop.async());

  /* issue a test smc call */
  fxl::Thread smc_test_thread([&] {
    long smc_ret = -1;
    smc32_args_t expect_smc_args = {
        .smc_nr = SMC_SC_VIRTIO_START,
        .params = {0x123U, 0x456U, 0x789U},
    };
    zx_status_t st = zx_smc_call_test(smc_handle, &expect_smc_args,
                                      sizeof(smc32_args_t), &smc_ret);
    ASSERT_EQ(st, ZX_OK);
    EXPECT_EQ(smc_args.smc_nr, expect_smc_args.smc_nr);
    EXPECT_EQ(smc_args.params[0], expect_smc_args.params[0]);
    EXPECT_EQ(smc_args.params[1], expect_smc_args.params[1]);
    EXPECT_EQ(smc_args.params[2], expect_smc_args.params[2]);
    EXPECT_EQ(smc_ret, 0);
  });
  EXPECT_TRUE(smc_test_thread.Run());

  /* receive smc call once and leave the loop */
  loop.Run(zx::time::infinite(), true);

  EXPECT_TRUE(smc_test_thread.Join());

  zx_handle_close(smc_handle);
  zx_handle_close(vmo_handle);
}

} // namespace smc_service
