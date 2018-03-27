// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <virtio/virtio.h>
#include <virtio/virtio_ring.h>

#include "garnet/lib/trusty/tipc_device.h"
#include "lib/fxl/logging.h"

namespace trusty {

zx_status_t TipcDevice::Create(VirtioBus* bus,
                               const Config* cfg,
                               fbl::RefPtr<TipcDevice>* out) {
  fbl::AllocChecker ac;
  auto dev = fbl::AdoptRef(new (&ac) TipcDevice());
  if (!ac.check())
    return ZX_ERR_NO_MEMORY;

  // Add to VirtioBus since we need to get vdev_id first
  zx_status_t status = bus->AddDevice(dev);
  if (status != ZX_OK)
    return status;

  tipc_vdev_descr* descr = &dev->descr_;
  descr->hdr.type = RSC_VDEV;
  descr->vdev.id = kVirtioTipcId;
  descr->vdev.config_len = sizeof(tipc_dev_config);
  descr->vdev.num_of_vrings = kNumQueues;
  descr->vdev.notifyid = dev->get_vdev_id();
  descr->vrings[0].align = PAGE_SIZE;
  descr->vrings[0].num = cfg->tx_buf_size;
  descr->vrings[0].notifyid = 1;
  descr->vrings[1].align = PAGE_SIZE;
  descr->vrings[1].num = cfg->rx_buf_size;
  descr->vrings[1].notifyid = 2;
  descr->config.msg_buf_max_size = PAGE_SIZE;
  descr->config.msg_buf_alignment = PAGE_SIZE;
  std::strncpy(descr->config.dev_name, cfg->dev_name, kTipcMaxDevNameLen);

  *out = dev;
  return ZX_OK;
}

zx_status_t TipcDevice::ValidateDescriptor(tipc_vdev_descr* vdev_descr) {
  if (vdev_descr->hdr.type != RSC_VDEV) {
    FXL_LOG(ERROR) << "unexpected type " << vdev_descr->hdr.type;
    return ZX_ERR_INVALID_ARGS;
  }

  if (vdev_descr->vdev.id != kVirtioTipcId) {
    FXL_LOG(ERROR) << "unexpected vdev id " << vdev_descr->vdev.id;
    return ZX_ERR_INVALID_ARGS;
  }

  if (vdev_descr->vdev.num_of_vrings != kNumQueues) {
    FXL_LOG(ERROR) << "unexpected number of vrings ("
                   << vdev_descr->vdev.num_of_vrings << " vs. " << kNumQueues
                   << ")";
    return ZX_ERR_INVALID_ARGS;
  }

  // check if NS driver successfully initilized
  if (vdev_descr->vdev.status !=
      (VIRTIO_STATUS_ACKNOWLEDGE | VIRTIO_STATUS_DRIVER |
       VIRTIO_STATUS_DRIVER_OK)) {
    FXL_LOG(ERROR) << "unexpected status" << vdev_descr->vdev.status;
    return ZX_ERR_INVALID_ARGS;
  }

  return ZX_OK;
}

zx_status_t TipcDevice::Probe(void* rsc_entry) {
  auto descr = reinterpret_cast<tipc_vdev_descr*>(rsc_entry);

  zx_status_t status = ValidateDescriptor(descr);
  if (status != ZX_OK)
    return status;

  if (get_state() != State::RESET)
    return ZX_ERR_BAD_STATE;

  for (uint8_t i = 0; i < descr->vdev.num_of_vrings; i++) {
    fw_rsc_vdev_vring* vring_descr = &descr->vrings[i];
    machina::VirtioQueue* queue = &queues_[i];

    // on archs with 64 bits phys addresses we store top 32 bits of
    // vring phys address in 'reserved' field of vring desriptor structure,
    // otherwise it set to 0.
    uint64_t pa64 = ((uint64_t)vring_descr->reserved << 32) | vring_descr->da;
    size_t size = vring_size(vring_descr->num, vring_descr->align);

    // translate the paddr and check if it is within shared memory
    void* buf = shared_mem_->PhysToVirt<void>(pa64, size);
    if (!buf)
      return ZX_ERR_OUT_OF_RANGE;

    vring vring;
    vring_init(&vring, vring_descr->num, buf, vring_descr->align);

    queue->set_size(vring_descr->num);
    queue->set_desc_addr(reinterpret_cast<uintptr_t>(vring.desc));
    queue->set_avail_addr(reinterpret_cast<uintptr_t>(vring.avail));
    queue->set_used_addr(reinterpret_cast<uintptr_t>(vring.used));
  }

  return ZX_OK;
}  // namespace trusty

}  // namespace trusty
