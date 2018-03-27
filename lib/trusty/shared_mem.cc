// Copyright 2018 OpenTrustGroup. All rights reserved.
// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fbl/alloc_checker.h>
#include <zircon/device/sysinfo.h>
#include <zircon/process.h>
#include <zircon/status.h>
#include <zx/vmar.h>

#include <fcntl.h>
#include <unistd.h>

#include "garnet/lib/trusty/shared_mem.h"

static constexpr uint32_t kMapFlags =
    ZX_VM_FLAG_PERM_READ | ZX_VM_FLAG_PERM_WRITE | ZX_VM_FLAG_MAP_RANGE;

namespace trusty {

zx_status_t SharedMem::Create(zx::vmo vmo, fbl::RefPtr<SharedMem>* out) {
  size_t vmo_size;
  zx_status_t status = vmo.get_size(&vmo_size);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to get vmo size: " << status;
    return status;
  }

  uintptr_t vaddr;
  status = zx::vmar::root_self().map(0, vmo, 0, vmo_size, kMapFlags, &vaddr);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to map vmo: " << status;
    return status;
  }

  uintptr_t paddr;
  status = vmo.op_range(ZX_VMO_OP_LOOKUP, 0, PAGE_SIZE, &paddr, sizeof(paddr));
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to do vmo lookup: " << status;
    return status;
  }

  fbl::AllocChecker ac;
  *out = fbl::AdoptRef(new (&ac)
                           SharedMem(fbl::move(vmo), vmo_size, vaddr, paddr));
  if (!ac.check()) {
    return ZX_ERR_NO_MEMORY;
  }

  return ZX_OK;
}

SharedMem::~SharedMem() {
  if (vaddr_ != 0) {
    zx::vmar::root_self().unmap(vaddr_, vmo_size_);
  }
}

}  // namespace trusty
