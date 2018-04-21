// Copyright 2018 Open Trust Group.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <fbl/array.h>
#include <fbl/auto_lock.h>
#include <fbl/function.h>

#include <lib/async/default.h>
#include <zircon/syscalls.h>
#include <zircon/syscalls/smc.h>

#include <array>
#include <unordered_map>

#include "garnet/lib/trusty/shared_mem.h"
#include "lib/fxl/logging.h"

namespace smc_service {

using trusty::SharedMem;
using SmcFunction = fbl::Function<long(smc32_args_t* args)>;

class SmcEntity {
 public:
  SmcEntity() {}

  virtual ~SmcEntity() {}

  virtual long InvokeSmcFunction(smc32_args_t* args) {
    return SM_ERR_UNDEFINED_SMC;
  };
};

class SmcService : public fbl::RefCounted<SmcService> {
 public:
  static zx_status_t Create(fbl::RefPtr<SmcService>* out);

  SmcService(zx_handle_t smc, fbl::RefPtr<SharedMem> shm)
      : smc_handle_(smc), shared_mem_(shm) {
    smc_entities_.reset(new fbl::unique_ptr<SmcEntity>[SMC_NUM_ENTITIES], SMC_NUM_ENTITIES);
  }

  ~SmcService() {
    zx_handle_close(smc_handle_);
    smc_entities_.reset();
  }

  void Init();

  void AddSmcEntity(uint32_t entity_nr, SmcEntity* e);

  zx_status_t HandleSmc();

  fbl::RefPtr<SharedMem> GetSharedMem() { return shared_mem_; };

 private:
  SmcEntity* GetSmcEntity(uint32_t entity_nr);

  mutable fbl::Mutex lock_;
  zx_handle_t smc_handle_;
  fbl::RefPtr<SharedMem> shared_mem_;
  fbl::Array<fbl::unique_ptr<SmcEntity>> smc_entities_ __TA_GUARDED(lock_);
};

} // namespace smc_service
