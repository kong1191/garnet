// Copyright 2018 Open Trust Group.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "garnet/bin/gzos/smc_service/smc_service.h"
#include "garnet/lib/trusty/virtio_device.h"

#include <lib/zx/channel.h>

namespace smc_service {

class TrustySmcEntity final : public SmcEntity {
 public:
  TrustySmcEntity(async_t* async, fbl::RefPtr<SharedMem> shm);
  ~TrustySmcEntity() {}

  zx_status_t Init() override;
  long InvokeSmcFunction(smc32_args_t* args) override;

 private:
  async_t* async_;
  fbl::RefPtr<SharedMem> shared_mem_;
  fbl::unique_ptr<trusty::VirtioBus> vbus_;
};

} // namespace smc_service
