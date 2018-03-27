// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <virtio/virtio.h>
#include <virtio/virtio_ring.h>

#include "garnet/lib/trusty/tipc_device.h"
#include "lib/fxl/logging.h"

namespace trusty {

zx_status_t TipcDevice::Create(VirtioBus* bus,
                               const tipc_vdev_descr* descr,
                               fbl::RefPtr<TipcDevice>* out) {
  fbl::AllocChecker ac;
  auto dev = fbl::AdoptRef(new (&ac) TipcDevice());
  if (!ac.check())
    return ZX_ERR_NO_MEMORY;

  // Add to VirtioBus since we need to get vdev_id first
  zx_status_t status = bus->AddDevice(dev);
  if (status != ZX_OK)
    return status;

  dev->descr_ = *descr;
  dev->descr_.vdev.notifyid = dev->vdev_id();
  *out = dev;
  return ZX_OK;
}

zx_status_t TipcDevice::ValidateDescriptor(tipc_vdev_descr* descr) {
  if (descr->hdr.type != RSC_VDEV) {
    FXL_LOG(ERROR) << "unexpected type " << descr->hdr.type;
    return ZX_ERR_INVALID_ARGS;
  }

  if (descr->vdev.id != kTipcVirtioDeviceId) {
    FXL_LOG(ERROR) << "unexpected vdev id " << descr->vdev.id;
    return ZX_ERR_INVALID_ARGS;
  }

  if (descr->vdev.num_of_vrings != kTipcNumQueues) {
    FXL_LOG(ERROR) << "unexpected number of vrings ("
                   << descr->vdev.num_of_vrings << " vs. " << kTipcNumQueues
                   << ")";
    return ZX_ERR_INVALID_ARGS;
  }

  // check if NS driver successfully initilized
  if (descr->vdev.status != (VIRTIO_STATUS_ACKNOWLEDGE | VIRTIO_STATUS_DRIVER |
                             VIRTIO_STATUS_DRIVER_OK)) {
    FXL_LOG(ERROR) << "unexpected status" << descr->vdev.status;
    return ZX_ERR_INVALID_ARGS;
  }

  return ZX_OK;
}

zx_status_t TipcDevice::InitializeVring(fw_rsc_vdev* descr) {
  for (uint8_t i = 0; i < descr->num_of_vrings; i++) {
    fw_rsc_vdev_vring* vring_descr = &descr->vring[i];
    machina::VirtioQueue* queue = &queues_[i];

    // on archs with 64 bits phys addresses we store top 32 bits of
    // vring phys address in 'reserved' field of vring desriptor structure,
    // otherwise it set to 0.
    uint64_t pa64 = ((uint64_t)vring_descr->reserved << 32) | vring_descr->da;
    size_t size = vring_size(vring_descr->num, vring_descr->align);

    // translate the paddr and check if it is within shared memory
    auto buf = shared_mem_->PhysToVirt<void>(pa64, size);
    if (!buf) {
      return ZX_ERR_OUT_OF_RANGE;
    }

    vring vring;
    vring_init(&vring, vring_descr->num, buf, vring_descr->align);

    queue->set_size(vring_descr->num);
    queue->set_desc_addr(reinterpret_cast<uintptr_t>(vring.desc));
    queue->set_avail_addr(reinterpret_cast<uintptr_t>(vring.avail));
    queue->set_used_addr(reinterpret_cast<uintptr_t>(vring.used));
  }

  return ZX_OK;
}

zx_status_t TipcDevice::Probe(void* rsc_entry) {
  auto descr = reinterpret_cast<tipc_vdev_descr*>(rsc_entry);

  zx_status_t status = ValidateDescriptor(descr);
  if (status != ZX_OK) {
    return status;
  }

  if (state() != State::RESET) {
    return ZX_ERR_BAD_STATE;
  }

  status = InitializeVring(&descr->vdev);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to initialize vring: " << status;
    return status;
  }

  return ZX_OK;
}  // namespace trusty

}  // namespace trusty
