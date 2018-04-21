// Copyright 2018 Open Trust Group.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "garnet/bin/gzos/smc_service/smc_service.h"
#include "garnet/lib/trusty/virtio_device.h"

namespace smc_service {

class TrustySmcEntity final : public SmcEntity {
 public:
  TrustySmcEntity(fbl::RefPtr<SharedMem> shm);
  ~TrustySmcEntity() {}

  long InvokeSmcFunction(smc32_args_t* args) override;

 private:
  fbl::unique_ptr<trusty::VirtioBus> vbus_;
};

} // namespace smc_service
