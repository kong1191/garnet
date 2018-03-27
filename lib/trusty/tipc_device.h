// Copyright 2018 OpenTrustGroup. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <fbl/alloc_checker.h>
#include <fbl/auto_lock.h>
#include <fbl/mutex.h>
#include <zircon/compiler.h>
#include <zircon/types.h>

#include "garnet/lib/trusty/virtio_device.h"
#include "garnet/lib/trusty/virtio_queue.h"

namespace trusty {

constexpr int kVirtioTipcId = 13;

constexpr char kTipcMaxDevNameLen = 32;

constexpr uint8_t kNumQueues = 2;
static_assert(kNumQueues % 2 == 0, "There must be a queue for both RX and TX");

/*
 *  Trusty IPC device configuration shared with linux side
 */
struct tipc_dev_config {
  uint32_t msg_buf_max_size;  /* max msg size that this device can handle */
  uint32_t msg_buf_alignment; /* required msg alignment (PAGE_SIZE) */
  char dev_name[kTipcMaxDevNameLen]; /* NS device node name  */
} __PACKED;

struct tipc_vdev_descr {
  struct fw_rsc_hdr hdr;
  struct fw_rsc_vdev vdev;
  struct fw_rsc_vdev_vring vrings[kNumQueues];
  struct tipc_dev_config config;
} __PACKED;

class TipcDevice : public VirtioDevice {
 public:
  struct Config {
    const char* dev_name;
    size_t tx_buf_size;
    size_t rx_buf_size;
  };

  static zx_status_t Create(VirtioBus* bus,
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
    descr->vdev.notifyid = dev->vdev_id_;
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

  zx_status_t ValidateDescriptor(tipc_vdev_descr* vdev_descr) {
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

  ~TipcDevice() override {}

  size_t ResourceEntrySize(void) override { return sizeof(tipc_vdev_descr); }
  void GetResourceEntry(void* rsc_entry) override {
    memcpy(rsc_entry, &descr_, sizeof(descr_));
  };
  zx_status_t Probe(void* rsc_entry) override {
    auto descr = reinterpret_cast<tipc_vdev_descr*>(rsc_entry);

    return ValidateDescriptor(descr);
  }

  machina::VirtioQueue* tx_queue() { return &queues_[0]; }
  machina::VirtioQueue* rx_queue() { return &queues_[1]; }

 private:
  TipcDevice() {
    for (uint8_t i = 0; i < kNumQueues; i++) {
	  queues_[i].set_device(this);
	}
  }

  // Queue for handling block requests.
  machina::VirtioQueue queues_[kNumQueues];
  // Resource entry descriptor for this device
  tipc_vdev_descr descr_ = {};
};

}  // namespace trusty
