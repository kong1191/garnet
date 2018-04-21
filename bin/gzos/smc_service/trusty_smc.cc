// Copyright 2018 Open Trust Group.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/gzos/smc_service/trusty_smc.h"
#include "garnet/lib/trusty/tipc_device.h"

#include "lib/fxl/arraysize.h"

namespace trusty {
  constexpr tipc_vdev_descr kTipcDescriptors[] = {
      DECLARE_TIPC_DEVICE_DESCR("dev0", 32, 32),
  };
}

namespace smc_service {

using trusty::kTipcDescriptors;
using trusty::VirtioBus;
using trusty::TipcDevice;

TrustySmcEntity::TrustySmcEntity(fbl::RefPtr<SharedMem> shm) {

  // Create VirtioBus
  fbl::AllocChecker ac;
  vbus_ = fbl::make_unique_checked<trusty::VirtioBus>(&ac, shm);
  if (!ac.check()) {
    FXL_LOG(ERROR) << "Failed to create virtio bus object";
    return;
  }

  // Create tipc devices on the bus
  for (uint32_t i = 0; i < arraysize(kTipcDescriptors); i++) {
    fbl::RefPtr<TipcDevice> dev =
        fbl::AdoptRef(new trusty::TipcDevice(kTipcDescriptors[i]));
    if (dev == nullptr) {
      FXL_LOG(ERROR) << "Failed to create tipc device object: "
          << kTipcDescriptors[i].config.dev_name;
      return;
    }

    zx_status_t status = vbus_->AddDevice(dev);
    if (status != ZX_OK) {
      FXL_LOG(ERROR) << "Failed to add tipc device to virtio bus: "
          << kTipcDescriptors[i].config.dev_name;
      return;
    }
  }
}

long TrustySmcEntity::InvokeSmcFunction(smc32_args_t* args) {
  long res;

  switch (args->smc_nr) {
//  case SMC_SC_VIRTIO_GET_DESCR:
//    res = get_ns_mem_buf(args, &ns_pa, &ns_sz, &ns_mmu_flags);
//    if (res == NO_ERROR)
//      res = virtio_get_description(ns_pa, ns_sz, ns_mmu_flags);
//    break;
//
//  case SMC_SC_VIRTIO_START:
//    res = get_ns_mem_buf(args, &ns_pa, &ns_sz, &ns_mmu_flags);
//    if (res == NO_ERROR)
//      res = virtio_start(ns_pa, ns_sz, ns_mmu_flags);
//    break;

  default:
    FXL_DLOG(ERROR) << "unknown smc function: 0x" << std::hex << SMC_FUNCTION(args->smc_nr);
    res = SM_ERR_NOT_SUPPORTED;
    break;
  }

  return res;
}

} // namespace smc_service
