// Copyright 2018 OpenTrustGroup. All rights reserved.
// Copyright 2015 Google, Inc. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <fbl/mutex.h>
#include <fbl/ref_counted.h>
#include <fbl/ref_ptr.h>
#include <fbl/unique_ptr.h>
#include <zx/vmo.h>

#include "garnet/lib/trusty/shared_mem.h"
#include "garnet/lib/trusty/third_party/remoteproc/remoteproc.h"

namespace trusty {

static constexpr uint8_t kMaxVirtioDevices = 10u;

static constexpr uint8_t kVirtioRscVersion = 1u;

class VirtioDevice : public fbl::RefCounted<VirtioDevice> {
 public:
  virtual ~VirtioDevice(){};
  virtual size_t ResourceEntrySize(void) = 0;
  virtual void GetResourceEntry(void* rsc_entry) = 0;

 protected:
  SharedMemPtr shared_mem_;

  uint32_t vdev_id_;

 private:
  friend class VirtioBus;

  // offset of resource entry within resource table
  uint32_t rsc_entry_offset_;
};

class VirtioBus {
 public:
  VirtioBus(SharedMemPtr shared_mem) : shared_mem_(shared_mem) {}

  zx_status_t AddDevice(fbl::RefPtr<VirtioDevice> vdev);
  zx_status_t GetResourceTable(void* buf_addr, size_t buf_size);

 private:
  enum class State {
    UNINITIALIZED = 0,
    IDLE = 1,
    ACTIVATING = 2,
    ACTIVE = 3,
    DEACTIVATING = 4,
  };

  SharedMemPtr shared_mem_;

  void FinalizeVdevRegisteryLocked(void) __TA_REQUIRES(mutex_);

  size_t rsc_table_size_ __TA_GUARDED(mutex_) = 0;
  uint8_t next_vdev_id_ __TA_GUARDED(mutex_) = 0;
  uint8_t vdev_count_ __TA_GUARDED(mutex_) = 0;

  State state_ __TA_GUARDED(mutex_) = State::UNINITIALIZED;
  fbl::Mutex mutex_;

  fbl::RefPtr<VirtioDevice> vdevs_[kMaxVirtioDevices] = {};
};

}  // namespace trusty
