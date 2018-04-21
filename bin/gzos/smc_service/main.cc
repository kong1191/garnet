// Copyright 2018 Open Trust Group.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdio.h>

#include <lib/async/cpp/loop.h>

#include "garnet/bin/gzos/smc_service/smc_service.h"
#include "garnet/bin/gzos/smc_service/trusty_smc.h"

using smc_service::SmcService;
using smc_service::TrustySmcEntity;

int main(int argc, const char** argv) {
  fbl::RefPtr<SmcService> service;

  zx_status_t status = SmcService::Create(&service);
  if (status != ZX_OK)
    return 1;

  service->AddSmcEntity(SMC_ENTITY_TRUSTED_OS, new TrustySmcEntity(service->GetSharedMem()));

  async_loop_config_t config = {
      .make_default_for_current_thread = true,
  };
  async::Loop loop(&config);

  loop.RunUntilIdle();

  fprintf(stderr, "smc service finished\n");

  return 0;
}
