// Copyright 2018 OpenTrustGroup. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <fbl/alloc_checker.h>
#include <fbl/unique_ptr.h>
#include <virtio/virtio.h>
#include <virtio/virtio_ring.h>

#include "garnet/lib/trusty/shared_mem.h"
#include "garnet/lib/trusty/tipc_device.h"
#include "garnet/lib/trusty/virtio_queue_fake.h"
#include "magma_util/simple_allocator.h"

namespace trusty {

// This class emulates a fake Linux client that sent command to us
class LinuxFake {
 public:
  static fbl::unique_ptr<LinuxFake> Create(fbl::RefPtr<SharedMem> shared_mem) {
    auto alloc =
        magma::SimpleAllocator::Create(shared_mem->addr(), shared_mem->size());
    if (!alloc)
      return NULL;

    return fbl::unique_ptr<LinuxFake>(
        new LinuxFake(std::move(alloc), shared_mem));
  }

  void* AllocBuffer(size_t size) {
    uint64_t buf;
    if (alloc_->Alloc(size, 0, &buf))
      return reinterpret_cast<void*>(buf);
    else
      return nullptr;
  }

  void FreeBuffer(void* addr) {
    alloc_->Free(reinterpret_cast<uint64_t>(addr));
  }

  zx_status_t AllocVring(fw_rsc_vdev_vring* vring) {
    size_t size = vring_size(vring->num, vring->align);

    void* buf = AllocBuffer(size);
    if (!buf)
      return ZX_ERR_NO_MEMORY;

    uint64_t paddr = shared_mem_->VirtToPhys(buf, size);
    vring->da = (paddr & 0xffffffff);
    vring->reserved = (paddr >> 32);

    return ZX_OK;
  }

  zx_status_t HandleResourceTable(resource_table* table) {
    // Create Vring for each Tipc devices
    for (uint32_t i = 0; i < table->num; i++) {
      tipc_vdev_descr* descr = reinterpret_cast<tipc_vdev_descr*>(
          reinterpret_cast<uint8_t*>(table) + table->offset[i]);
      zx_status_t status;

      status = AllocVring(&descr->vrings[0]);
      if (status != ZX_OK)
        return status;

      status = AllocVring(&descr->vrings[1]);
      if (status != ZX_OK)
        return status;

      descr->vdev.status = VIRTIO_STATUS_ACKNOWLEDGE | VIRTIO_STATUS_DRIVER |
                           VIRTIO_STATUS_DRIVER_OK;
    }
    return ZX_OK;
  }

 private:
  LinuxFake(std::unique_ptr<magma::SimpleAllocator> alloc,
            fbl::RefPtr<SharedMem> shared_mem)
      : alloc_(std::move(alloc)), shared_mem_(shared_mem) {}

  std::unique_ptr<magma::SimpleAllocator> alloc_;

  fbl::RefPtr<SharedMem> shared_mem_;
};

// This class emulates a fake Linux tipc driver
class TipcDriverFake {
 public:
  TipcDriverFake(TipcDevice* device, LinuxFake* linux)
      : rx_queue_(device->tx_queue()),
        tx_queue_(device->rx_queue()),
        device_(device),
        linux_(linux) {}
  ~TipcDriverFake() {
    if (buf_) {
      linux_->FreeBuffer(buf_);
    }
  }

  zx_status_t AllocVring(machina::VirtioQueueFake* queue,
                         fw_rsc_vdev_vring* vring) {
    size_t size = vring_size(vring->num, vring->align);

    buf_ = linux_->AllocBuffer(size);
    if (!buf_)
      return ZX_ERR_NO_MEMORY;

    vring->da = device_->shared_mem()->VirtToPhys(buf_, size);
    return ZX_OK;
  }

  zx_status_t Probe(fw_rsc_vdev* vdev) {
    zx_status_t status;

    status = AllocVring(&rx_queue_, &vdev->vring[0]);
    if (status != ZX_OK)
      return status;

    status = AllocVring(&tx_queue_, &vdev->vring[1]);
    if (status != ZX_OK)
      return status;

    vdev->status = VIRTIO_STATUS_ACKNOWLEDGE | VIRTIO_STATUS_DRIVER |
                   VIRTIO_STATUS_DRIVER_OK;
    return ZX_OK;
  }

 private:
  machina::VirtioQueueFake rx_queue_;
  machina::VirtioQueueFake tx_queue_;
  TipcDevice* device_ = nullptr;
  LinuxFake* linux_ = nullptr;
  void* buf_ = nullptr;
};

}  // namespace trusty
