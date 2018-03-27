// Copyright 2018 OpenTrustGroup. All rights reserved.
// Copyright 2015 Google, Inc. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <fbl/mutex.h>
#include <fbl/ref_counted.h>
#include <fbl/ref_ptr.h>
#include <fbl/unique_ptr.h>
#include <fbl/vector.h>
#include <zx/vmo.h>

#include "garnet/lib/trusty/shared_mem.h"
#include "garnet/lib/trusty/third_party/remoteproc/remoteproc.h"

namespace trusty {

static constexpr uint8_t kVirtioResourceTableVersion = 1u;

template <typename T>
static inline T* rsc_entry(resource_table* table, uint8_t idx) {
  ZX_DEBUG_ASSERT(idx < table->num);
  return reinterpret_cast<T*>(reinterpret_cast<uint8_t*>(table) +
                              table->offset[idx]);
}

static inline uint32_t rsc_table_hdr_size(uint32_t vdev_count) {
  return sizeof(struct resource_table) + sizeof(uint32_t) * vdev_count;
}

class VirtioDevice : public fbl::RefCounted<VirtioDevice> {
 public:
  VirtioDevice() : vdev_id_(next_vdev_id_++){};
  virtual ~VirtioDevice(){};
  virtual size_t ResourceEntrySize(void) = 0;
  virtual void GetResourceEntry(void* rsc_entry) = 0;
  virtual zx_status_t Probe(void* rsc_entry) = 0;

  SharedMem* shared_mem() { return shared_mem_.get(); }

  // Returns true if the set of features have been negotiated to be enabled.
  bool has_enabled_features(uint32_t features) {
    fbl::AutoLock lock(&mutex_);
    return (features_ & driver_features_ & features) == features;
  }

 protected:
  enum class State {
    RESET = 0,
    ACTIVE = 1,
  };

  fbl::RefPtr<SharedMem> shared_mem_;

  uint32_t vdev_id(void) { return vdev_id_; }

  State state(void) {
    fbl::AutoLock lock(&mutex_);
    return state_;
  }
  void set_state(State state) {
    fbl::AutoLock lock(&mutex_);
    state_ = state;
  }

 private:
  friend class VirtioBus;
  // This field is assigned by VirtioBus when the device is added
  uint32_t vdev_id_;
  static uint32_t next_vdev_id_;

  // offset of resource entry within resource table
  uint32_t rsc_entry_offset_;

  fbl::Mutex mutex_;
  State state_ __TA_GUARDED(mutex_) = State::RESET;

  // Device feature bits.
  //
  // Defined in Virtio 1.0 Section 2.2.
  uint32_t features_ __TA_GUARDED(mutex_) = 0;

  // Driver feature bits.
  uint32_t driver_features_ __TA_GUARDED(mutex_) = 0;
};

class VirtioBus {
 public:
  VirtioBus(fbl::RefPtr<SharedMem> shared_mem) : shared_mem_(shared_mem) {}

  zx_status_t AddDevice(fbl::RefPtr<VirtioDevice> vdev);
  zx_status_t GetResourceTable(void* buf, size_t* buf_size);
  zx_status_t Start(void* buf, size_t buf_size);

  const auto& devices() { return vdevs_; }

 private:
  enum class State {
    UNINITIALIZED = 0,
    IDLE = 1,
    ACTIVE = 2,
  };

  void FinalizeVdevRegisteryLocked(void) __TA_REQUIRES(mutex_);

  size_t rsc_table_size_ __TA_GUARDED(mutex_) = 0;

  State state_ __TA_GUARDED(mutex_) = State::UNINITIALIZED;
  fbl::Mutex mutex_;

  fbl::RefPtr<SharedMem> shared_mem_;
  fbl::Vector<fbl::RefPtr<VirtioDevice>> vdevs_ __TA_GUARDED(mutex_);
};

}  // namespace trusty
