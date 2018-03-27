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
  TipcDevice(tipc_vdev_descr* descr);
  ~TipcDevice() override;

  size_t ResourceEntrySize(void) override { return sizeof(tipc_vdev_descr); }
  void GetResourceEntry(void* rsc_entry) override {
    *reinterpret_cast<tipc_vdev_descr*>(rsc_entry) = descr_;
  };

  machina::VirtioQueue* tx_queue() { return &queues_[0]; }
  machina::VirtioQueue* rx_queue() { return &queues_[1]; }

 private:
  // Queue for handling block requests.
  machina::VirtioQueue queues_[kNumQueues];
  // Resource entry descriptor for this device
  tipc_vdev_descr descr_ = {};
};

class TipcDeviceBuilder {
 public:
  TipcDeviceBuilder& SetDevName(const char* name) {
    dev_name_ = name;
    return *this;
  }
  TipcDeviceBuilder& SetTxBufSize(size_t size) {
    tx_buf_size_ = size;
    return *this;
  }
  TipcDeviceBuilder& SetRxBufSize(size_t size) {
    rx_buf_size_ = size;
    return *this;
  }
  TipcDeviceBuilder& SetNotifyId(uint32_t id) {
    notify_id_ = id;
    return *this;
  }

  zx_status_t Build(fbl::unique_ptr<TipcDevice>* out) {
    descr_.hdr.type = RSC_VDEV;
    descr_.vdev.config_len = sizeof(tipc_dev_config);
    descr_.vdev.num_of_vrings = kNumQueues;
    descr_.vdev.notifyid = notify_id_;
    descr_.vrings[0].align = PAGE_SIZE;
    descr_.vrings[0].num = tx_buf_size_;
    descr_.vrings[0].notifyid = 1;
    descr_.vrings[1].align = PAGE_SIZE;
    descr_.vrings[1].num = rx_buf_size_;
    descr_.vrings[1].notifyid = 2;
    descr_.config.msg_buf_max_size = PAGE_SIZE;
    descr_.config.msg_buf_alignment = PAGE_SIZE;
    std::strncpy(descr_.config.dev_name, dev_name_, kTipcMaxDevNameLen);

    fbl::AllocChecker ac;
    auto dev = fbl::make_unique_checked<TipcDevice>(&ac, &descr_);

    if (!ac.check())
      return ZX_ERR_NO_MEMORY;

    *out = fbl::move(dev);
    return ZX_OK;
  }

 private:
  uint32_t notify_id_;
  uint32_t tx_buf_size_;
  uint32_t rx_buf_size_;
  const char* dev_name_;
  tipc_vdev_descr descr_ = {};
};

}  // namespace trusty
