// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Copyright 2015 Google, Inc. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <virtio/virtio.h>
#include <virtio/virtio_ring.h>

#include "garnet/lib/trusty/tipc_device.h"
#include "garnet/lib/trusty/tipc_msg.h"
#include "lib/fxl/logging.h"

namespace trusty {

TipcDevice::TipcDevice(const tipc_vdev_descr& descr,
                       async_t* async,
                       zx::channel channel)
    : descr_(descr),
      channel_(fbl::move(channel)),
      rx_stream_(async, rx_queue(), channel_.get()),
      tx_stream_(async, tx_queue(), channel_.get()) {
  descr_.vdev.notifyid = notify_id();

  for (uint8_t i = 0; i < kTipcNumQueues; i++) {
    queues_[i].set_device(this);
  }
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
    FXL_LOG(ERROR) << "unexpected status: " << descr->vdev.status;
    return ZX_ERR_INVALID_ARGS;
  }

  return ZX_OK;
}

zx_status_t TipcDevice::InitializeVring(fw_rsc_vdev* descr) {
  for (uint8_t i = 0; i < descr->num_of_vrings; i++) {
    fw_rsc_vdev_vring* vring_descr = &descr->vring[i];
    VirtioQueue* queue = &queues_[i];

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
    vring_init(&vring, vring_descr->num, (void*)pa64, vring_descr->align);

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

  status = Online();
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to go online: " << status;
    return status;
  }

  status = rx_stream_.Start();
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to start rx stream: " << status;
	return status;
  }

  status = tx_stream_.Start();
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to start tx stream: " << status;
	return status;
  }

  set_state(State::ACTIVE);
  return ZX_OK;
}

zx_status_t TipcDevice::Online() {
  tipc_ctrl_msg_hdr ctrl_msg = {
      .type = CtrlMessage::GO_ONLINE,
      .body_len = 0,
  };
  return SendCtrlMessage(&ctrl_msg, sizeof(ctrl_msg));
}

zx_status_t TipcDevice::SendCtrlMessage(void* data, uint16_t data_len) {
  uint16_t desc_index;
  zx_status_t status = tx_queue()->NextAvail(&desc_index);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "No available buffer: " << status;
    return status;
  }

  virtio_desc_t desc;
  status = tx_queue()->ReadDesc(desc_index, &desc);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Read descriptor failed: " << status;
    return status;
  }

  if (desc.has_next) {
    FXL_LOG(ERROR) << "Only single buffer is supported";
    return ZX_ERR_IO;
  }

  if (desc.len < sizeof(tipc_hdr) + data_len) {
    FXL_LOG(ERROR) << "Buffer size (" << desc.len << ") is too small";
    return ZX_ERR_NO_MEMORY;
  }

  auto hdr = reinterpret_cast<tipc_hdr*>(desc.addr);
  *hdr = (tipc_hdr){
      .src = kTipcCtrlAddress,
      .dst = kTipcCtrlAddress,
      .reserved = 0,
      .len = data_len,
      .flags = 0,
  };
  memcpy(hdr->data, data, data_len);

  status = tx_queue()->Return(desc_index, sizeof(tipc_hdr) + data_len);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to return buffer: " << status;
    return status;
  }

  return ZX_OK;
}

TipcDevice::Stream::Stream(async_t* async,
                           VirtioQueue* queue,
                           zx_handle_t channel)
    : async_(async), channel_(channel), queue_(queue), queue_wait_(async) {
  channel_wait_.set_handler(
      fbl::BindMember(this, &TipcDevice::Stream::OnChannelReady));
}

zx_status_t TipcDevice::Stream::Start() {
  return WaitOnQueue();
}

void TipcDevice::Stream::Stop() {
  channel_wait_.Cancel(async_);
  queue_wait_.Cancel();
}

zx_status_t TipcDevice::Stream::WaitOnQueue() {
  return queue_wait_.Wait(
      queue_, fbl::BindMember(this, &TipcDevice::Stream::OnQueueReady));
}

void TipcDevice::Stream::OnQueueReady(zx_status_t status, uint16_t index) {
  if (status == ZX_OK) {
    head_ = index;
    status = queue_->ReadDesc(head_, &desc_);
  }

  if (status == ZX_ERR_OUT_OF_RANGE) {
    DropBuffer();
    return;
  }

  if (status != ZX_OK) {
    OnStreamClosed(status, "reading descriptor");
    return;
  }

  status = WaitOnChannel();
  if (status != ZX_OK) {
    OnStreamClosed(status, "waiting on channel");
  }
}

void TipcDevice::Stream::DropBuffer() {
  FXL_LOG(WARNING) << "buffer not in shared memory, drop it";

  zx_status_t status = queue_->Return(head_, 0);
  if (status != ZX_OK) {
    FXL_LOG(WARNING) << "Failed to return descriptor " << status;
  }

  status = WaitOnQueue();
  if (status != ZX_OK) {
    OnStreamClosed(status, "dropping buffer");
  }
}

zx_status_t TipcDevice::Stream::WaitOnChannel() {
  zx_signals_t signals = ZX_CHANNEL_PEER_CLOSED;
  signals |= desc_.writable ? ZX_CHANNEL_READABLE : ZX_CHANNEL_WRITABLE;
  channel_wait_.set_object(channel_);
  channel_wait_.set_trigger(signals);
  return channel_wait_.Begin(async_);
}

async_wait_result_t TipcDevice::Stream::OnChannelReady(
    async_t* async,
    zx_status_t status,
    const zx_packet_signal_t* signal) {
  if (status != ZX_OK) {
    OnStreamClosed(status, "async wait on channel");
    return ASYNC_WAIT_FINISHED;
  }

  const bool do_read = desc_.writable;
  uint32_t actual;
  if (do_read) {
    status = zx_channel_read(channel_, 0, static_cast<void*>(desc_.addr), NULL,
                             desc_.len, 0, &actual, NULL);

    // no message in channel
    if (status == ZX_ERR_SHOULD_WAIT) {
      return ASYNC_WAIT_AGAIN;
    }
  } else {
    status = zx_channel_write(channel_, 0, static_cast<const void*>(desc_.addr),
                              desc_.len, NULL, 0);
  }

  if (status != ZX_OK) {
    OnStreamClosed(status, do_read ? "read from channel" : "write to channel");
    return ASYNC_WAIT_FINISHED;
  }

  status = queue_->Return(head_, do_read ? actual : 0);
  if (status != ZX_OK) {
    FXL_LOG(WARNING) << "Failed to return descriptor " << status;
  }

  status = WaitOnQueue();
  if (status != ZX_OK) {
    OnStreamClosed(status, "wait on queue");
  }
  return ASYNC_WAIT_FINISHED;
}

void TipcDevice::Stream::OnStreamClosed(zx_status_t status,
                                        const char* action) {
  Stop();
  FXL_LOG(ERROR) << "Stream closed during step '" << action << "' (" << status
                 << ")";
}

}  // namespace trusty
