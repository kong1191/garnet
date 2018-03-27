// Copyright 2018 OpenTrustGroup. All rights reserved.
// Copyright 2015 Google, Inc. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fbl/auto_lock.h>
#include <zircon/assert.h>

#include "garnet/lib/trusty/shared_mem.h"
#include "garnet/lib/trusty/virtio_device.h"

namespace trusty {

zx_status_t VirtioBus::AddDevice(fbl::RefPtr<VirtioDevice> vdev) {
  fbl::AutoLock mutex(&mutex_);

  if (vdev_count_ == kMaxVirtioDevices)
    return ZX_ERR_OUT_OF_RANGE;

  if (state_ != State::UNINITIALIZED)
    return ZX_ERR_BAD_STATE;

  uint32_t vdev_id = next_vdev_id_++;
  ZX_DEBUG_ASSERT(vdevs_[vdev_id] == nullptr);

  vdev->vdev_id_ = vdev_id;
  vdev->shared_mem_ = shared_mem_;
  vdevs_[vdev_id] = vdev;
  vdev_count_++;

  return ZX_OK;
}

zx_status_t VirtioBus::GetResourceTable(void* buf_addr, size_t buf_size) {
  fbl::AutoLock mutex(&mutex_);
  FinalizeVdevRegisteryLocked();

  if (buf_size < rsc_table_size_)
    return ZX_ERR_NO_MEMORY;

  if (!shared_mem_->validate_vaddr_range(buf_addr, buf_size))
    return ZX_ERR_OUT_OF_RANGE;

  resource_table* table = reinterpret_cast<resource_table*>(buf_addr);
  table->ver = kVirtioRscVersion;
  table->num = vdev_count_;
  for (uint8_t i = 0; i < vdev_count_; i++) {
    VirtioDevice* vd = vdevs_[i].get();
    uint32_t offset = vd->rsc_entry_offset_;
    size_t size = vd->ResourceEntrySize();

    ZX_DEBUG_ASSERT(offset <= rsc_table_size_);
    ZX_DEBUG_ASSERT(offset + size <= rsc_table_size_);

    table->offset[i] = offset;
    vd->GetResourceEntry(table + offset);
  }

  return ZX_OK;
}

void VirtioBus::FinalizeVdevRegisteryLocked(void) {
  if (state_ == State::UNINITIALIZED) {
    uint32_t offset =
        sizeof(struct resource_table) + sizeof(uint32_t) * vdev_count_;

    // go through the list of vdev and calculate total resource table size
    // and resource entry offset withing resource table buffer
    for (uint8_t i = 0; i < vdev_count_; i++) {
      VirtioDevice* vd = vdevs_[i].get();
      vd->rsc_entry_offset_ = offset;
      offset += vd->ResourceEntrySize();
    }

    rsc_table_size_ = offset;
    state_ = State::IDLE;
  }
}

}  // namespace trusty
