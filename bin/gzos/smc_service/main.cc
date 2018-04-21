// Copyright 2018 Open Trust Group.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>

#include <zircon/process.h>
#include <zircon/processargs.h>

#include "garnet/bin/gzos/smc_service/smc_service.h"
#include "garnet/bin/gzos/smc_service/trusty_smc.h"

using smc_service::SmcService;
using smc_service::TrustySmcEntity;

int main(int argc, const char** argv) {
  zx_handle_t smc_handle = ZX_HANDLE_INVALID;
  zx_handle_t vmo_handle = ZX_HANDLE_INVALID;
  zx_info_smc_t smc_info = {};
  zx_status_t status = zx_smc_create(0, &smc_info, sizeof(zx_info_smc_t),
                                     &smc_handle, &vmo_handle);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to create smc kernel object, status=" << status;
    return 1;
  }

  SmcService* s = SmcService::Create(smc_handle, vmo_handle, smc_info);
  if (s == nullptr) {
    zx_handle_close(smc_handle);
    zx_handle_close(vmo_handle);
    return 1;
  }

  async::Loop loop(&kAsyncLoopConfigMakeDefault);

  zx_handle_t ree_agent_handle = zx_get_startup_handle(PA_HND(PA_USER0, 0));
  if (ree_agent_handle == ZX_HANDLE_INVALID) {
    FXL_LOG(ERROR) << "Unable to get default channel to ree agent";
    return 1;
  }

  s->AddSmcEntity(
      SMC_ENTITY_TRUSTED_OS,
      new TrustySmcEntity(loop.async(), ree_agent_handle, s->GetSharedMem()));

  s->Start(loop.async());

  loop.Run();

  FXL_LOG(INFO) << "smc service finished";

  return 0;
}
