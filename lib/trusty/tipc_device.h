// Copyright 2018 OpenTrustGroup. All rights reserved.
// Copyright 2015 Google, Inc. All rights reserved.
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

static constexpr int kTipcVirtioDeviceId = 13;

static constexpr char kTipcMaxNameLength = 32;

static constexpr uint8_t kTipcTxQueue = 0;
static constexpr uint8_t kTipcRxQueue = 1;
static constexpr uint8_t kTipcNumQueues = 2;
static_assert(kTipcNumQueues % 2 == 0,
              "There must be a queue for both RX and TX");

// Trusty IPC device configuration shared with linux side
struct tipc_dev_config {
  uint32_t msg_buf_max_size;   // max msg size that this device can handle
  uint32_t msg_buf_alignment;  // required msg alignment (PAGE_SIZE)
  char dev_name[kTipcMaxNameLength];  // NS device node name
} __PACKED;

struct tipc_vdev_descr {
  struct fw_rsc_hdr hdr;
  struct fw_rsc_vdev vdev;
  struct fw_rsc_vdev_vring vrings[kTipcNumQueues];
  struct tipc_dev_config config;
} __PACKED;

#define DECLARE_TIPC_DEVICE_DESCR(_nd_name, _txvq_sz, _rxvq_sz) \
  {                                                             \
    .hdr.type = RSC_VDEV,                                       \
    .vdev =                                                     \
        {                                                       \
            .id = kTipcVirtioDeviceId,                          \
            .notifyid = 0,                                      \
            .dfeatures = 0,                                     \
            .config_len = sizeof(struct tipc_dev_config),       \
            .num_of_vrings = kTipcNumQueues,                    \
        },                                                      \
    .vrings =                                                   \
        {                                                       \
            [kTipcTxQueue] =                                    \
                {                                               \
                    .align = PAGE_SIZE,                         \
                    .num = (_txvq_sz),                          \
                    .notifyid = 1,                              \
                },                                              \
            [kTipcRxQueue] =                                    \
                {                                               \
                    .align = PAGE_SIZE,                         \
                    .num = (_rxvq_sz),                          \
                    .notifyid = 2,                              \
                },                                              \
        },                                                      \
    .config = {                                                 \
      .msg_buf_max_size = PAGE_SIZE,                            \
      .msg_buf_alignment = PAGE_SIZE,                           \
      .dev_name = _nd_name,                                     \
    }                                                           \
  }

class TipcDevice : public VirtioDevice {
 public:
  static zx_status_t Create(VirtioBus* bus,
                            const tipc_vdev_descr* descr,
                            fbl::RefPtr<TipcDevice>* out);

  ~TipcDevice() override {}

  size_t ResourceEntrySize(void) override { return sizeof(tipc_vdev_descr); }
  void GetResourceEntry(void* rsc_entry) override {
    memcpy(rsc_entry, &descr_, sizeof(descr_));
  };
  zx_status_t Probe(void* rsc_entry) override;

  VirtioQueue* tx_queue() { return &queues_[kTipcTxQueue]; }
  VirtioQueue* rx_queue() { return &queues_[kTipcRxQueue]; }

 private:
  TipcDevice() {
    for (uint8_t i = 0; i < kTipcNumQueues; i++) {
      queues_[i].set_device(this);
    }
  }

  zx_status_t ValidateDescriptor(tipc_vdev_descr* descr);
  zx_status_t InitializeVring(fw_rsc_vdev* descr);
  zx_status_t SendCtrlMessage(void* data, uint16_t data_len);
  zx_status_t Online();

  // Queue for handling block requests.
  VirtioQueue queues_[kTipcNumQueues];
  // Resource entry descriptor for this device
  tipc_vdev_descr descr_ = {};
};

}  // namespace trusty
