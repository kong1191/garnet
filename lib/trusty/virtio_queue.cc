// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/trusty/virtio_queue.h"

#include <stdio.h>
#include <string.h>

#include <fbl/unique_ptr.h>
#include <virtio/virtio_ring.h>

#include "garnet/lib/trusty/virtio_device.h"
#include "lib/fxl/logging.h"

namespace trusty {

VirtioQueue::VirtioQueue() : device_(nullptr) {
  FXL_CHECK(zx::event::create(0, &event_) == ZX_OK);
}

template <typename T>
static void queue_set_segment_addr(VirtioQueue* queue,
                                   uint64_t guest_paddr,
                                   size_t size,
                                   T** ptr) {
  trusty::VirtioDevice* device = queue->device();
  *ptr = device->shared_mem()->PhysToVirt<T>(guest_paddr, size);
}

uint16_t VirtioQueue::size() const {
  fbl::AutoLock lock(&mutex_);
  return ring_.size;
}

void VirtioQueue::set_size(uint16_t size) {
  fbl::AutoLock lock(&mutex_);
  ring_.size = size;
}

uint16_t VirtioQueue::avail_event_num() {
  fbl::AutoLock lock(&mutex_);
  return avail_event_num_;
}

void VirtioQueue::set_avail_event_num(uint16_t num) {
  fbl::AutoLock lock(&mutex_);
  avail_event_num_ = num;
}

void VirtioQueue::set_desc_addr(uint64_t desc_paddr) {
  fbl::AutoLock lock(&mutex_);
  ring_.addr.desc = desc_paddr;
  uintptr_t desc_size = ring_.size * sizeof(ring_.desc[0]);
  queue_set_segment_addr(this, desc_paddr, desc_size, &ring_.desc);
}

uint64_t VirtioQueue::desc_addr() const {
  fbl::AutoLock lock(&mutex_);
  return ring_.addr.desc;
}

void VirtioQueue::set_avail_addr(uint64_t avail_paddr) {
  fbl::AutoLock lock(&mutex_);
  ring_.addr.avail = avail_paddr;
  uintptr_t avail_size =
      sizeof(*ring_.avail) + (ring_.size * sizeof(ring_.avail->ring[0]));
  queue_set_segment_addr(this, avail_paddr, avail_size, &ring_.avail);

  uintptr_t used_event_paddr = avail_paddr + avail_size;
  uintptr_t used_event_size = sizeof(*ring_.used_event);
  queue_set_segment_addr(this, used_event_paddr, used_event_size,
                         &ring_.used_event);
}

uint64_t VirtioQueue::avail_addr() const {
  fbl::AutoLock lock(&mutex_);
  return ring_.addr.avail;
}

void VirtioQueue::set_used_addr(uint64_t used_paddr) {
  fbl::AutoLock lock(&mutex_);
  ring_.addr.used = used_paddr;
  uintptr_t used_size =
      sizeof(*ring_.used) + (ring_.size * sizeof(ring_.used->ring[0]));
  queue_set_segment_addr(this, used_paddr, used_size, &ring_.used);

  uintptr_t avail_event_paddr = used_paddr + used_size;
  uintptr_t avail_event_size = sizeof(*ring_.avail_event);
  queue_set_segment_addr(this, avail_event_paddr, avail_event_size,
                         &ring_.avail_event);
}

uint64_t VirtioQueue::used_addr() const {
  fbl::AutoLock lock(&mutex_);
  return ring_.addr.used;
}

zx_status_t VirtioQueue::Signal() {
  fbl::AutoLock lock(&mutex_);
  if (HasAvailLocked()) {
    return event_.signal(0, SIGNAL_QUEUE_AVAIL);
  }
  return ZX_OK;
}

zx_status_t VirtioQueue::NextAvailLocked(uint16_t* index) {
  if (!HasAvailLocked()) {
    return ZX_ERR_SHOULD_WAIT;
  }

  *index = ring_.avail->ring[RingIndexLocked(ring_.index++)];

  // If we have event indicies enabled, update the avail-event to notify us
  // when we have sufficient descriptors available.
  if (device()->has_enabled_features(1u << VIRTIO_F_RING_EVENT_IDX) &&
      ring_.avail_event) {
    *ring_.avail_event = ring_.index + avail_event_num_ - 1;
  }

  if (!HasAvailLocked()) {
    return event_.signal(SIGNAL_QUEUE_AVAIL, 0);
  }
  return ZX_OK;
}

zx_status_t VirtioQueue::NextAvail(uint16_t* index) {
  fbl::AutoLock lock(&mutex_);
  return NextAvailLocked(index);
}

bool VirtioQueue::HasAvailLocked() const {
  if (ring_.avail == nullptr) {
    return false;
  }
  return ring_.avail->idx != ring_.index;
}

uint32_t VirtioQueue::RingIndexLocked(uint32_t index) const {
  return index % ring_.size;
}

void VirtioQueue::Wait(uint16_t* index) {
  zx_status_t status;
  while ((status = NextAvail(index)) == ZX_ERR_SHOULD_WAIT) {
    event_.wait_one(SIGNAL_QUEUE_AVAIL, zx::time::infinite(), nullptr);
  }
  FXL_CHECK(status == ZX_OK);
}

struct poll_task_args_t {
  VirtioQueue* queue;
  virtio_queue_poll_fn_t handler;
  std::string name;
  void* ctx;
};

static int virtio_queue_poll_task(void* ctx) {
  zx_status_t result = ZX_OK;
  fbl::unique_ptr<poll_task_args_t> args(static_cast<poll_task_args_t*>(ctx));
  while (true) {
    uint16_t descriptor;
    args->queue->Wait(&descriptor);

    uint32_t used = 0;
    zx_status_t status =
        args->handler(args->queue, descriptor, &used, args->ctx);
    result = args->queue->Return(descriptor, used);
    if (result != ZX_OK) {
      FXL_LOG(ERROR) << "Failed to return descriptor to queue " << result;
      break;
    }

    if (status == ZX_ERR_STOP) {
      break;
    }
    if (status != ZX_OK) {
      FXL_LOG(ERROR) << "Error " << status
                     << " while handling queue buffer for queue " << args->name;
      result = status;
      break;
    }
  }

  return result;
}

zx_status_t VirtioQueue::Poll(virtio_queue_poll_fn_t handler,
                              void* ctx,
                              std::string name) {
  auto args = new poll_task_args_t{this, handler, std::move(name), ctx};

  thrd_t thread;
  int ret = thrd_create_with_name(&thread, virtio_queue_poll_task, args,
                                  args->name.c_str());
  if (ret != thrd_success) {
    delete args;
    FXL_LOG(ERROR) << "Failed to create queue thread " << ret;
    return ZX_ERR_INTERNAL;
  }

  ret = thrd_detach(thread);
  if (ret != thrd_success) {
    FXL_LOG(ERROR) << "Failed to detach queue thread " << ret;
    return ZX_ERR_INTERNAL;
  }

  return ZX_OK;
}

zx_status_t VirtioQueue::PollAsync(async_t* async,
                                   async::Wait* wait,
                                   virtio_queue_poll_fn_t handler,
                                   void* ctx) {
  wait->set_object(event_.get());
  wait->set_trigger(SIGNAL_QUEUE_AVAIL);
  wait->set_handler([this, handler, ctx](async_t* async, async::Wait* wait,
                                         zx_status_t status,
                                         const zx_packet_signal_t* signal) {
    InvokeAsyncHandler(async, wait, status, handler, ctx);
  });
  return wait->Begin(async);
}

void VirtioQueue::InvokeAsyncHandler(async_t* async,
                                     async::Wait* wait,
                                     zx_status_t status,
                                     virtio_queue_poll_fn_t handler,
                                     void* ctx) {
  if (status != ZX_OK) {
    return;
  }

  uint16_t head;
  uint32_t used = 0;
  status = NextAvail(&head);
  if (status == ZX_OK) {
    status = handler(this, head, &used, ctx);
    // Try to return the buffer to the queue, even if the handler has failed
    // so we don't leak the descriptor.
    zx_status_t return_status = Return(head, used);
    if (status == ZX_OK) {
      status = return_status;
    }
  }
  if (status == ZX_OK || status == ZX_ERR_SHOULD_WAIT) {
    wait->Begin(async);  // ignore errors
  }
}

zx_status_t VirtioQueue::ReadDesc(uint16_t desc_index, virtio_desc_t* out) {
  fbl::AutoLock lock(&mutex_);
  auto& desc = ring_.desc[desc_index];

  // check if the paddr in the descriptor is under the range of shared memory
  // and translate it to host vaddr
  auto addr = device()->shared_mem()->PhysToVirt<void>(desc.addr, desc.len);
  if (!addr) {
    return ZX_ERR_OUT_OF_RANGE;
  }

  out->addr = addr;
  out->len = desc.len;
  out->has_next = desc.flags & VRING_DESC_F_NEXT;
  out->writable = desc.flags & VRING_DESC_F_WRITE;
  out->next = desc.next;
  return ZX_OK;
}

zx_status_t VirtioQueue::Return(uint16_t index,
                                uint32_t len,
                                InterruptAction action) {
  bool needs_interrupt = false;
  bool use_event_index =
      device()->has_enabled_features(1u << VIRTIO_F_RING_EVENT_IDX);
  {
    fbl::AutoLock lock(&mutex_);
    volatile struct vring_used_elem* used =
        &ring_.used->ring[RingIndexLocked(ring_.used->idx)];

    used->id = index;
    used->len = len;
    ring_.used->idx++;

    // Virtio 1.0 Section 2.4.7.2: Virtqueue Interrupt Suppression
    if (!use_event_index) {
      // If the VIRTIO_F_EVENT_IDX feature bit is not negotiated:
      //  - The device MUST ignore the used_event value.
      //  - After the device writes a descriptor index into the used ring:
      //    - If flags is 1, the device SHOULD NOT send an interrupt.
      //    - If flags is 0, the device MUST send an interrupt.
      needs_interrupt = ring_.used->flags == 0;
    } else {
      // Otherwise, if the VIRTIO_F_EVENT_IDX feature bit is negotiated:
      //
      //  - The device MUST ignore the lower bit of flags.
      //  - After the device writes a descriptor index into the used ring:
      //    - If the idx field in the used ring (which determined where that
      //      descriptor index was placed) was equal to used_event, the device
      //      MUST send an interrupt.
      //    - Otherwise the device SHOULD NOT send an interrupt.
      if (ring_.used_event) {
        needs_interrupt = ring_.used->idx == (*ring_.used_event + 1);
      }
    }
  }

  if (needs_interrupt) {
    // For current trusty implementation we don't need to notify
    // Linux. Linux will check the input queue each time an SM call
    // returns. So we have nothing to do here
  }
  return ZX_OK;
}

zx_status_t VirtioQueue::HandleDescriptor(virtio_queue_fn_t handler,
                                          void* ctx) {
  uint16_t head;
  uint32_t used_len = 0;
  uintptr_t mem_addr = device()->shared_mem()->addr();
  size_t mem_size = device()->shared_mem()->size();

  // Get the next descriptor from the available ring. If none are available
  // we can just no-op.
  zx_status_t status = NextAvail(&head);
  if (status == ZX_ERR_SHOULD_WAIT) {
    return ZX_OK;
  }
  if (status != ZX_OK) {
    return status;
  }

  status = ZX_OK;
  uint16_t desc_index = head;
  volatile const struct vring_desc* desc;
  do {
    if (desc_index >= size()) {
      return ZX_ERR_OUT_OF_RANGE;
    }
    {
      fbl::AutoLock lock(&mutex_);
      desc = &ring_.desc[desc_index];
    }

    const uint64_t end = desc->addr + desc->len;
    if (end < desc->addr || end > mem_size) {
      return ZX_ERR_OUT_OF_RANGE;
    }

    void* addr = reinterpret_cast<void*>(mem_addr + desc->addr);
    status = handler(addr, desc->len, desc->flags, &used_len, ctx);
    if (status != ZX_OK) {
      return status;
    }

    desc_index = desc->next;
  } while (desc->flags & VRING_DESC_F_NEXT);

  status = Return(head, used_len);
  if (status != ZX_OK) {
    return status;
  }
  fbl::AutoLock lock(&mutex_);
  return HasAvailLocked() ? ZX_ERR_NEXT : ZX_OK;
}

}  // namespace trusty
