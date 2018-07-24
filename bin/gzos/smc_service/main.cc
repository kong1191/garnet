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

static zx_status_t get_startup_channel(zx::channel* ree_agent_out) {
  zx_handle_t request = zx_take_startup_handle(PA_HND(PA_USER0, 0));
  if (request == ZX_HANDLE_INVALID) {
    FXL_LOG(ERROR) << "Can not get smc_service channel";
    return ZX_ERR_NO_RESOURCES;
  }

  ree_agent_out->reset(request);
  return ZX_OK;
}

int main(int argc, const char** argv) {
  zx::channel ree_agent_cli;
  zx_status_t status = get_startup_channel(&ree_agent_cli);
  if (status != ZX_OK)
    return 1;

  async::Loop loop(&kAsyncLoopConfigMakeDefault);

  SmcService* s = SmcService::GetInstance();
  if (s == nullptr)
    return 1;

  status = s->AddSmcEntity(
      SMC_ENTITY_TRUSTED_OS,
      new TrustySmcEntity(loop.async(), fbl::move(ree_agent_cli)));
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to add Trusty smc entity, status=" << status;
    return 1;
  }

  s->Start(loop.async());
  loop.Run();
  return 0;
}