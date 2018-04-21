// Copyright 2018 Open Trust Group.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async/cpp/loop.h>

#include "garnet/bin/gzos/smc_service/smc_service.h"
#include "gtest/gtest.h"

namespace smc_service {

class FakeSmcEntity : public SmcEntity {
public:
  long InvokeSmcFunction(smc32_args_t* args) {
    return args->smc_nr;
  }
};

TEST(SmcServiceTest, FakeSmc) {
  zx_handle_t smc_handle = ZX_HANDLE_INVALID;
  zx_handle_t vmo_handle = ZX_HANDLE_INVALID;

  zx_status_t status = zx_smc_create(0, &smc_handle, &vmo_handle);
  ASSERT_EQ(status, ZX_OK);

  SmcService* service = SmcService::Create(smc_handle, vmo_handle);
  ASSERT_TRUE(service != nullptr);

  async_loop_config_t config = {
      .make_default_for_current_thread = true,
  };
  async::Loop loop(&config);

  /* trigger fake smc request from smc kernel object */
  status = zx_object_signal(smc_handle, 0, ZX_SMC_FAKE_REQUEST);
  ASSERT_EQ(status, ZX_OK);

  service->AddSmcEntity(SMC_ENTITY(0x534d43U), new FakeSmcEntity());
  service->Start(loop.async());

  /* receive smc request once and leave the loop */
  loop.Run(ZX_TIME_INFINITE, true);

  /* wait for test result signal from smc kernel object */
  zx_signals_t sig = ZX_SIGNAL_NONE;
  status = zx_object_wait_one(smc_handle, ZX_USER_SIGNAL_ALL, ZX_TIME_INFINITE, &sig);
  EXPECT_EQ(status, ZX_OK);
  EXPECT_EQ(sig & ZX_USER_SIGNAL_ALL, ZX_SMC_TEST_PASS);

  zx_handle_close(smc_handle);
  zx_handle_close(vmo_handle);
}

} // namespace smc_service
