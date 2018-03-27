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
                            fbl::RefPtr<TipcDevice>* out);

  ~TipcDevice() override {}

  size_t ResourceEntrySize(void) override { return sizeof(tipc_vdev_descr); }
  void GetResourceEntry(void* rsc_entry) override {
    memcpy(rsc_entry, &descr_, sizeof(descr_));
  };
  zx_status_t Probe(void* rsc_entry) override;

  machina::VirtioQueue* tx_queue() { return &queues_[0]; }
  machina::VirtioQueue* rx_queue() { return &queues_[1]; }

 private:
  TipcDevice() {
    for (uint8_t i = 0; i < kNumQueues; i++) {
      queues_[i].set_device(this);
    }
  }
  zx_status_t ValidateDescriptor(tipc_vdev_descr* vdev_descr);

  // Queue for handling block requests.
  machina::VirtioQueue queues_[kNumQueues];
  // Resource entry descriptor for this device
  tipc_vdev_descr descr_ = {};
};

}  // namespace trusty
