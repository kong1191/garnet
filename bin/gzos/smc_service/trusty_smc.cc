// Copyright 2018 Open Trust Group.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/*
 * Copyright (c) 2013-2016, Google, Inc. All rights reserved
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files
 * (the "Software"), to deal in the Software without restriction,
 * including without limitation the rights to use, copy, modify, merge,
 * publish, distribute, sublicense, and/or sell copies of the Software,
 * and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include "garnet/bin/gzos/smc_service/trusty_smc.h"
#include "garnet/lib/trusty/tipc_device.h"

#include "lib/fxl/arraysize.h"

typedef uint64_t ns_addr_t;
typedef uint32_t ns_size_t;
typedef uint64_t ns_paddr_t;

// clang-format off
/* Normal memory */
#define NS_MAIR_NORMAL_CACHED_WB_RWA       0xFF /* inner and outer write back read/write allocate */
#define NS_MAIR_NORMAL_CACHED_WT_RA        0xAA /* inner and outer write through read allocate */
#define NS_MAIR_NORMAL_CACHED_WB_RA        0xEE /* inner and outer wriet back, read allocate */
#define NS_MAIR_NORMAL_UNCACHED            0x44 /* uncached */

/* sharaeble attributes */
#define NS_NON_SHAREABLE                   0x0
#define NS_OUTER_SHAREABLE                 0x2
#define NS_INNER_SHAREABLE                 0x3
// clang-format on

typedef struct ns_page_info {
  /* 48-bit physical address 47:12 */
  ns_paddr_t pa() { return attr & 0xFFFFFFFFF000ULL; }

  /* sharaeble attributes */
  uint64_t shareable() { return (attr >> 8) & 0x3; }

  /* cache attrs encoded in the top bits 55:49 of the PTE*/
  uint64_t mair() { return (attr >> 48) & 0xFF; }

  /* Access permissions AP[2:1]
   *    EL0   EL1
   * 00 None  RW
   * 01 RW    RW
   * 10 None  RO
   * 11 RO    RO
   */
  uint64_t ap() { return ((attr) >> 6) & 0x3; }
  bool ap_user() { return ap() & 0x1 ? true : false; }
  bool ap_ro() { return ap() & 0x2 ? true : false; }

  uint64_t attr;
} ns_page_info_t;

namespace trusty {
  constexpr tipc_vdev_descr kTipcDescriptors[] = {
      DECLARE_TIPC_DEVICE_DESCR("dev0", 32, 32),
  };
}

namespace smc_service {

using trusty::tipc_vdev_descr;
using trusty::kTipcDescriptors;
using trusty::VirtioBus;
using trusty::TipcDevice;

TrustySmcEntity::TrustySmcEntity(async_t* async,
                                 zx_handle_t ch,
                                 fbl::RefPtr<SharedMem> shm)
    : async_(async),
      ree_agent_ctrl_channel_(ch),
      shared_mem_(shm),
      vbus_(nullptr) {}

zx_status_t TrustySmcEntity::CreateReeAgentPerDeviceChannel(zx::channel* out) {
  // TODO(james): implement fidl interface for ree agent and virtio device
  //              to create per device channel.
  zx::channel ree_agent;
  return zx::channel::create(0u, &ree_agent, out);
}

zx_status_t TrustySmcEntity::Init() {
  // Create VirtioBus
  fbl::AllocChecker ac;
  vbus_ = fbl::make_unique_checked<trusty::VirtioBus>(&ac, shared_mem_);
  if (!ac.check()) {
    FXL_LOG(ERROR) << "Failed to create virtio bus object";
    return ZX_ERR_NO_MEMORY;
  }

  // Create tipc devices on the bus and make per device channels to ree agent
  for (uint32_t i = 0; i < arraysize(kTipcDescriptors); i++) {
    const tipc_vdev_descr* desc = &kTipcDescriptors[i];
    zx::channel per_device_channel;
    zx_status_t status = CreateReeAgentPerDeviceChannel(&per_device_channel);
    if (status != ZX_OK) {
      FXL_LOG(ERROR) << "Failed to create per device channel to ree agent: "
                     << desc->config.dev_name << " (" << status << ")";
      return status;
    }

    auto dev = fbl::MakeRefCountedChecked<trusty::TipcDevice>(
        &ac, kTipcDescriptors[i], async_, fbl::move(per_device_channel));
    if (!ac.check()) {
      FXL_LOG(ERROR) << "Failed to create tipc device object: "
                     << desc->config.dev_name;
      return ZX_ERR_NO_MEMORY;
    }

    status = vbus_->AddDevice(dev);
    if (status != ZX_OK) {
      FXL_LOG(ERROR) << "Failed to add tipc device to virtio bus: "
                     << desc->config.dev_name << " (" << status << ")";
      return status;
    }
  }

  return ZX_OK;
}

/* TODO(james): Currently we do not support dynamically mapping ns buffer.
 *              Do we still need to check memory attribute here?
 */
static zx_status_t check_mem_attr(ns_page_info_t pi, bool is_shm_use_cache) {
  bool is_mem_cached;

  switch (pi.mair()) {
    case NS_MAIR_NORMAL_CACHED_WB_RWA:
      is_mem_cached = true;
      break;
    case NS_MAIR_NORMAL_UNCACHED:
      is_mem_cached = false;
      break;
    default:
      FXL_DLOG(ERROR) << "Unsupported memory attr: 0x" << std::hex << pi.mair();
      return ZX_ERR_NOT_SUPPORTED;
  }

  if (is_shm_use_cache != is_mem_cached) {
    FXL_DLOG(ERROR) << "memory cache attribute is not aligned with ns share "
                       "memory cache policy";
    return ZX_ERR_INVALID_ARGS;
  }

  if (is_mem_cached) {
    if (pi.shareable() != NS_INNER_SHAREABLE) {
      FXL_DLOG(ERROR) << "Unsupported sharable attr: 0x" << std::hex
                      << pi.shareable();
      return ZX_ERR_NOT_SUPPORTED;
    }
  }

  if (pi.ap_user() || pi.ap_ro()) {
    FXL_DLOG(ERROR) << "Unexpected access permission: 0x" << std::hex
                    << pi.ap();
    return ZX_ERR_INVALID_ARGS;
  }

  return ZX_OK;
}

zx_status_t TrustySmcEntity::GetNsBuf(smc32_args_t* args,
                                      void** buf,
                                      size_t* size) {
  FXL_DCHECK(args != nullptr);
  FXL_DCHECK(size != nullptr);

  ns_paddr_t ns_buf_pa;
  ns_size_t ns_buf_sz;

  ns_page_info_t pi = {
      .attr = (static_cast<uint64_t>(args->params[1]) << 32) | args->params[0],
  };

  zx_status_t status = check_mem_attr(pi, shared_mem_->use_cache());
  if (status != ZX_OK) {
    return status;
  }

  ns_buf_pa = pi.pa();
  ns_buf_sz = static_cast<ns_size_t>(args->params[2]);

  void* tmp_buf = shared_mem_->PhysToVirt<void*>(ns_buf_pa, ns_buf_sz);
  if (tmp_buf == nullptr) {
    return ZX_ERR_OUT_OF_RANGE;
  }

  *buf = tmp_buf;
  *size = static_cast<size_t>(ns_buf_sz);
  return ZX_OK;
}

/*
 * Translate internal errors to SMC errors
 */
static long to_smc_error(long ret) {
  if (ret > 0)
    return ret;

  switch (ret) {
    case ZX_OK:
      return 0;

    case ZX_ERR_INVALID_ARGS:
      return SM_ERR_INVALID_PARAMETERS;

    case ZX_ERR_NOT_SUPPORTED:
      return SM_ERR_NOT_SUPPORTED;

    case ZX_ERR_BAD_STATE:
    case ZX_ERR_OUT_OF_RANGE:
    case ZX_ERR_ACCESS_DENIED:
    case ZX_ERR_IO:
      return SM_ERR_NOT_ALLOWED;

    default:
      return SM_ERR_INTERNAL_FAILURE;
  }
}

long TrustySmcEntity::InvokeSmcFunction(smc32_args_t* args) {
  long ret;
  zx_status_t status;
  void* ns_buf;
  size_t size;

  switch (args->smc_nr) {
    case SMC_SC_VIRTIO_GET_DESCR:
      status = GetNsBuf(args, &ns_buf, &size);
      if (status == ZX_OK) {
        status = vbus_->GetResourceTable(ns_buf, &size);
      }

      if (status == ZX_OK) {
        ret = size;
      } else {
        ret = status;
      }
      break;

    case SMC_SC_VIRTIO_START:
      status = GetNsBuf(args, &ns_buf, &size);
      if (status == ZX_OK) {
        status = vbus_->Start(ns_buf, size);
      }
      ret = status;
      break;

    default:
      FXL_DLOG(ERROR) << "unknown smc function: 0x" << std::hex
                      << SMC_FUNCTION(args->smc_nr);
      ret = ZX_ERR_NOT_SUPPORTED;
      break;
  }

  return to_smc_error(ret);
}

} // namespace smc_service
