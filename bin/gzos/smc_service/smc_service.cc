// Copyright 2018 Open Trust Group.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zx/vmo.h>

#include "garnet/bin/gzos/smc_service/smc_service.h"
#include "garnet/bin/gzos/smc_service/trusty_smc.h"

namespace smc_service {

zx_status_t SmcService::Create(fbl::RefPtr<SmcService>* out) {
  zx_handle_t smc_handle = ZX_HANDLE_INVALID;
  zx_handle_t shm_vmo_handle = ZX_HANDLE_INVALID;

  zx_status_t status = zx_smc_create(0, &smc_handle, &shm_vmo_handle);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to create smc object, status=" << status;
    return status;
  }

  fbl::RefPtr<SmcService> service;
  fbl::RefPtr<SharedMem> shared_mem;
  zx::vmo shm_vmo(shm_vmo_handle);
  status = SharedMem::Create(fbl::move(shm_vmo), &shared_mem);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to create shared memory object, status=" << status;
    goto fail;
  }

  service = fbl::AdoptRef<SmcService>(new SmcService(smc_handle, fbl::move(shared_mem)));
  if (service == nullptr) {
    FXL_LOG(ERROR) << "Failed to create smc service object due to not enough memory";
    status = ZX_ERR_NO_MEMORY;
    goto fail;
  }

  *out = service;
  return ZX_OK;

fail:
  zx_handle_close(smc_handle);
  return status;
}

void SmcService::AddSmcEntity(uint32_t entity_nr, SmcEntity* e) {
  fbl::AutoLock al(&lock_);
  if (smc_entities_[entity_nr] == nullptr && e != nullptr) {
    smc_entities_[entity_nr].reset(e);
  }
}

SmcEntity* SmcService::GetSmcEntity(uint32_t entity_nr) {
  fbl::AutoLock al(&lock_);
  return (entity_nr < SMC_NUM_ENTITIES) ? smc_entities_[entity_nr].get() : nullptr;
};

zx_status_t SmcService::HandleSmc() {
  smc32_args_t smc_args = {};
  zx_status_t status = zx_smc_wait_for_request(smc_handle_, &smc_args, sizeof(smc32_args_t));
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to wait for smc request, status=" << status;
    return status;
  }

  long result = SM_ERR_UNDEFINED_SMC;
  SmcEntity* entity = GetSmcEntity(SMC_ENTITY(smc_args.smc_nr));
  if (entity != nullptr)
    result = entity->InvokeSmcFunction(&smc_args);

  status = zx_smc_set_result(smc_handle_, result);
  FXL_DCHECK(status == ZX_OK);

  return status;
}

} // namespace smc_service

