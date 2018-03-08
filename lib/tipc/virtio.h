// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_MACHINA_VIRTIO_H_
#define GARNET_LIB_MACHINA_VIRTIO_H_

#include <fbl/auto_lock.h>
#include <fbl/mutex.h>
#include <hypervisor/io.h>
#include <hypervisor/phys_mem.h>
#include <virtio/virtio.h>
#include <zircon/compiler.h>
#include <zircon/types.h>

struct vring_desc;
struct vring_avail;
struct vring_used;

namespace machina {

typedef struct virtio_queue virtio_queue_t;

class VirtioDevice;

// Base class for all virtio devices.
class VirtioDevice {
 public:
  virtual ~VirtioDevice() = default;

  // Read a device-specific configuration field.
  virtual zx_status_t ReadConfig(uint64_t addr, IoValue* value);

  // Write a device-specific configuration field.
  virtual zx_status_t WriteConfig(uint64_t addr, const IoValue& value);

  // Handle notify events for one of this devices queues.
  virtual zx_status_t HandleQueueNotify(uint16_t queue_sel) { return ZX_OK; }

  // Send a notification back to the guest that there are new descriptors in
  // then used ring.
  //
  // The method for how this notification is delievered is transport
  // specific.
  zx_status_t NotifyGuest();

  const PhysMem& phys_mem() { return phys_mem_; }
  uint16_t num_queues() const { return num_queues_; }

  // ISR flag values.
  enum IsrFlags : uint8_t {
    // Interrupt is caused by a queue.
    VIRTIO_ISR_QUEUE = 0x1,
    // Interrupt is caused by a device config change.
    VIRTIO_ISR_DEVICE = 0x2,
  };

  // Sets the given flags in the ISR register.
  void add_isr_flags(uint8_t flags) {
    fbl::AutoLock lock(&mutex_);
    isr_status_ |= flags;
  }

  // Device features.
  //
  // These are feature bits that are supported by the device. They may or
  // may not correspond to the set of feature flags that have been negotiated
  // at runtime.
  void add_device_features(uint32_t features) {
    fbl::AutoLock lock(&mutex_);
    features_ |= features;
  }
  bool has_device_features(uint32_t features) {
    fbl::AutoLock lock(&mutex_);
    return (features_ & features) == features;
  }

 protected:
  VirtioDevice(uint8_t device_id,
               void* config,
               size_t config_size,
               virtio_queue_t* queues,
               uint16_t num_queues,
               const PhysMem& phys_mem);

  // Mutex for accessing device configuration fields.
  fbl::Mutex config_mutex_;

 private:
  // Temporarily expose our state to the PCI transport until the proper
  // accessor methods are defined.
  friend class VirtioPci;

  fbl::Mutex mutex_;

  // Handle kicks from the driver that a queue needs attention.
  zx_status_t Kick(uint16_t queue_sel);

  // Device feature bits.
  //
  // Defined in Virtio 1.0 Section 2.2.
  uint32_t features_ __TA_GUARDED(mutex_) = 0;
  uint32_t features_sel_ __TA_GUARDED(mutex_) = 0;

  // Driver feature bits.
  uint32_t driver_features_ __TA_GUARDED(mutex_) = 0;
  uint32_t driver_features_sel_ __TA_GUARDED(mutex_) = 0;

  // Virtio device id.
  const uint8_t device_id_;

  // Device status field as defined in Virtio 1.0, Section 2.1.
  uint8_t status_ __TA_GUARDED(mutex_) = 0;

  // Interrupt status register.
  uint8_t isr_status_ __TA_GUARDED(mutex_) = 0;

  // Index of the queue currently selected by the driver.
  uint16_t queue_sel_ __TA_GUARDED(mutex_) = 0;

  // Pointer to the structure that holds this devices configuration
  // structure.
  void* const device_config_ __TA_GUARDED(config_mutex_) = nullptr;

  // Number of bytes used for this devices configuration space.
  //
  // This should cover only bytes used for the device-specific portions of
  // the configuration header, omitting any of the (transport-specific)
  // shared configuration space.
  const size_t device_config_size_ = 0;

  // Virtqueues for this device.
  virtio_queue_t* const queues_ = nullptr;

  // Size of queues array.
  const uint16_t num_queues_ = 0;

  // Guest physical memory.
  const PhysMem& phys_mem_;
};

// Stores the Virtio queue based on the ring provided by the guest.
//
// NOTE(abdulla): This structure points to guest-controlled memory.
typedef struct virtio_queue {
  mtx_t mutex;
  // Allow threads to block on buffers in the avail ring.
  cnd_t avail_ring_cnd;

  // Queue addresses as defined in Virtio 1.0 Section 4.1.4.3.
  union {
    struct {
      uint64_t desc;
      uint64_t avail;
      uint64_t used;
    };

    // Software will access these using 32 bit operations. Provide a
    // convenience interface for these use cases.
    uint32_t words[6];
  } addr;

  // Number of entries in the descriptor table.
  uint16_t size;
  uint16_t index;

  // Pointer to the owning device.
  VirtioDevice* virtio_device;

  volatile struct vring_desc* desc;  // guest-controlled

  volatile struct vring_avail* avail;  // guest-controlled
  volatile uint16_t* used_event;       // guest-controlled

  volatile struct vring_used* used;  // guest-controlled
  volatile uint16_t* avail_event;    // guest-controlled
} virtio_queue_t;

// Callback function for virtio_queue_handler.
//
// For chained buffers uing VRING_DESC_F_NEXT, this function will be called once
// for each buffer in the chain.
//
// addr     - Pointer to the descriptor buffer.
// len      - Length of the descriptor buffer.
// flags    - Flags from the vring descriptor.
// used     - To be incremented by the number of bytes used from addr.
// ctx      - The same pointer passed to virtio_queue_handler.
typedef zx_status_t (*virtio_queue_fn_t)(void* addr,
                                         uint32_t len,
                                         uint16_t flags,
                                         uint32_t* used,
                                         void* ctx);

// Handles the next available descriptor in a Virtio queue, calling handler to
// process individual payload buffers.
//
// On success the function either returns ZX_OK if there are no more descriptors
// available, or ZX_ERR_NEXT if there are more available descriptors to process.
zx_status_t virtio_queue_handler(virtio_queue_t* queue,
                                 virtio_queue_fn_t handler,
                                 void* ctx);

// Get the index of the next descriptor in the available ring.
//
// If a buffer is a available, the descriptor index is written to |index|, the
// queue index pointer is incremented, and ZX_OK is returned.
//
// If no buffers are available ZX_ERR_NOT_FOUND is returned.
zx_status_t virtio_queue_next_avail(virtio_queue_t* queue, uint16_t* index);

// Blocking variant of virtio_queue_next_avail.
void virtio_queue_wait(virtio_queue_t* queue, uint16_t* index);

// Notify waiting threads blocked on |virtio_queue_wait| that the avail ring
// has descriptors available.
void virtio_queue_signal(virtio_queue_t* queue);

// Sets the address of the descriptor table for this queue.
void virtio_queue_set_desc_addr(virtio_queue_t* queue, uint64_t desc_addr);

// Sets the address of the available ring for this queue.
void virtio_queue_set_avail_addr(virtio_queue_t* queue, uint64_t avail_addr);

// Sets the address of the used ring for this queue.
void virtio_queue_set_used_addr(virtio_queue_t* queue, uint64_t used_addr);

// Callback for virtio_queue_poll.
//
// queue    - The queue being polled.
// head     - Descriptor index of the buffer chain to process.
// used     - To be incremented by the number of bytes used from addr.
// ctx      - The same pointer passed to virtio_queue_poll.
//
// The queue will continue to be polled as long as this method returns ZX_OK.
// The error ZX_ERR_STOP will be treated as a special value to indicate queue
// polling should stop gracefully and terminate the thread.
//
// Any other error values will be treated as unexpected errors that will cause
// the polling thread to be terminated with a non-zero exit value.
typedef zx_status_t (*virtio_queue_poll_fn_t)(virtio_queue_t* queue,
                                              uint16_t head,
                                              uint32_t* used,
                                              void* ctx);

// Spawn a thread to wait for descriptors to be available and invoke the
// provided handler on each available buffer asyncronously.
zx_status_t virtio_queue_poll(virtio_queue_t* queue,
                              virtio_queue_poll_fn_t handler,
                              void* ctx,
                              const char* thread_name);

// A higher-level API for vring_desc.
typedef struct virtio_desc {
  // Pointer to the buffer in our address space.
  void* addr;
  // Number of bytes at addr.
  uint32_t len;
  // Is there another buffer after this one?
  bool has_next;
  // Only valid if has_next is true.
  uint16_t next;
  // If true, this buffer must only be written to (no reads). Otherwise this
  // buffer must only be read from (no writes).
  bool writable;
} virtio_desc_t;

// Reads a single descriptor from the queue.
//
// This method should only be called using descriptor indicies acquired with
// virtio_queue_next_avail (including any chained decriptors) and before
// they've been released with virtio_queue_return.
zx_status_t virtio_queue_read_desc(virtio_queue_t* queue,
                                   uint16_t index,
                                   virtio_desc_t* desc);

// Return a descriptor to the used ring.
//
// |index| must be a value received from a call to virtio_queue_next_avail. Any
// buffers accessed via |index| or any chained descriptors must not be used
// after calling virtio_queue_return.
void virtio_queue_return(virtio_queue_t* queue, uint16_t index, uint32_t len);

}  // namespace machina

#endif  // GARNET_LIB_MACHINA_VIRTIO_H_
