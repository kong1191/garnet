// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <fbl/unique_ptr.h>
#include <virtio/virtio.h>
#include <virtio/virtio_ring.h>

#include "garnet/lib/trusty/virtio_queue.h"

struct fw_rsc_vdev_vring;

namespace trusty {

class VirtioQueueFake;

// Helper class for building buffer made up of chained descriptors.
//
// When building a descriptor chain, any errors are deferred until a call to
// DescBuilder::Build in order to make the interface more fluent.
class DescBuilder {
 public:
  DescBuilder& Append(void* addr, size_t size, bool writeable);
  DescBuilder& Append(uintptr_t addr, size_t size, bool writeable) {
    return Append(reinterpret_cast<void*>(addr), size, writeable);
  }

  // Adds a buffer to the chain that is flagged as device writeable.
  DescBuilder& AppendWriteable(void* addr, size_t size) {
    return Append(addr, size, true);
  }
  DescBuilder& AppendWriteable(uintptr_t addr, size_t size) {
    return Append(addr, size, true);
  }

  // Adds a buffer to the chain that is flagged as device writeable.
  DescBuilder& AppendReadable(void* addr, size_t size) {
    return Append(addr, size, false);
  }
  DescBuilder& AppendReadable(uintptr_t addr, size_t size) {
    return Append(addr, size, false);
  }

  // Make this descriptor chain visible to the device by writing the head
  // index to the available ring and incrementing the available index.
  //
  // The index of the head descriptor is written to |desc| if it is non-null.
  zx_status_t Build(uint16_t* desc);
  zx_status_t Build() { return Build(nullptr); }

 private:
  friend class VirtioQueueFake;
  DescBuilder(VirtioQueueFake* queue) : queue_(queue) {}

  VirtioQueueFake* queue_;
  size_t len_ = 0;
  uint16_t prev_desc_ = 0;
  uint16_t head_desc_ = 0;
  zx_status_t status_ = ZX_OK;
};

// Helper class for creating fake virtio queue requests.
//
// The device should be initialized with guest physmem at 0 so that the
// simulated guest physical address space aliases our address space.
class VirtioQueueFake {
 public:
  explicit VirtioQueueFake(VirtioQueue* queue) : queue_(queue) {}
  ~VirtioQueueFake();

  // Initialize the Queue based on given vring descriptor
  void Init(fw_rsc_vdev_vring* vring_desc);

  // Access the underlying VirtioQueue.
  VirtioQueue* queue() const { return queue_; }

  // Allocate and write a descriptor. |addr|, |len|, and |flags| correspond
  // to the fields in vring_desc.
  //
  // The index of the allocated descriptor is written to |desc|.
  //
  // Descriptors are not reclaimed and it is a programming error to attempt
  // to write to more than descriptors than the queue was initialized with.
  // ZX_ERR_NO_MEMORY is returned if the pool of available desciptors has
  // been exhausted.
  zx_status_t WriteDescriptor(void* addr,
                              size_t len,
                              uint16_t flags,
                              uint16_t* desc);

  // Write to |desc| that it is continued via |next|.
  //
  // Returns ZX_ERR_INVALID_ARGS if |desc| or |next| are greater than the
  // queue size.
  zx_status_t SetNext(uint16_t desc, uint16_t next);

  // Writes |desc| to the next entry in the available ring, making the
  // descriptor chain visible to the device.
  void WriteToAvail(uint16_t desc);

  // Remove a buffer from used ring, and return the buffer descriptor idx
  volatile vring_used_elem* ReadFromUsed();

  DescBuilder BuildDescriptor() { return DescBuilder(this); }

 private:
  uint16_t queue_size_;
  VirtioQueue* queue_;

  // The next entry in the descriptor table that is available.
  uint16_t next_free_desc_ = 0;

  uint16_t last_used_idx_ = 0;
};

}  // namespace trusty
