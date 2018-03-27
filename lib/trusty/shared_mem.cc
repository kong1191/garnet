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
    ZX_VM_FLAG_PERM_READ | ZX_VM_FLAG_PERM_WRITE;

namespace trusty {

// TODO(sy): the root reousrce from sysinfo is too powerful.
// Get restricted root resource from SM object after it's ready
static zx_status_t get_root_resource(zx_handle_t* root_resource) {
  int fd = open("/dev/misc/sysinfo", O_RDWR);
  if (fd < 0) {
    FXL_LOG(ERROR) << "ERROR: Cannot open sysinfo: " << strerror(errno);
    return ZX_ERR_NOT_FOUND;
  }

  ssize_t n = ioctl_sysinfo_get_root_resource(fd, root_resource);
  close(fd);
  if (n != sizeof(*root_resource)) {
    if (n < 0) {
      FXL_LOG(ERROR) << "ERROR: Cannot obtain root resource: "
                     << zx_status_get_string(n);
      return (zx_status_t)n;
    } else {
      FXL_LOG(ERROR) << "ERROR: Cannot obtain root resource (" << n
                     << " != " << sizeof(root_resource) << ")";
      return ZX_ERR_NOT_FOUND;
    }
  }
  return ZX_OK;
}

zx_status_t SharedMem::Create(size_t size, fbl::RefPtr<SharedMem>* out) {
  zx_handle_t root_resource;
  zx_status_t status = get_root_resource(&root_resource);
  if (status != ZX_OK) {
    return status;
  }

  zx_handle_t vmo_handle;
  status = zx_vmo_create_contiguous(root_resource, size, 0, &vmo_handle);
  if (status != ZX_OK) {
    return status;
  }

  zx::vmo vmo(vmo_handle);
  size_t vmo_size;
  status = vmo.get_size(&vmo_size);
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
