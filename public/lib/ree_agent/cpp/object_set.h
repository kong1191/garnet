// Copyright 2018 Open Trust Group
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <fbl/auto_lock.h>
#include <fbl/mutex.h>
#include <fbl/intrusive_double_list.h>
#include <fbl/ref_ptr.h>
#include <ree_agent/cpp/fidl.h>
#include <zx/port.h>

#include "lib/fxl/logging.h"
#include "lib/fxl/synchronization/thread_annotations.h"
#include "lib/ree_agent/cpp/object.h"
namespace ree_agent {

class TipcObjectSet : public TipcObject {
 public:
  TipcObjectSet() {
    zx_status_t status = zx::port::create(0, &port_);
    FXL_CHECK(status == ZX_OK);
  }
  zx_status_t AddObject(fbl::RefPtr<TipcObject> obj);
  void RemoveObject(fbl::RefPtr<TipcObject> obj);

  virtual zx_status_t Wait(WaitResult* result, zx::time deadline) override;

 private:
  ObjectType get_type() override { return ObjectType::OBJECT_SET; }

  bool in_object_list(fbl::RefPtr<TipcObject> obj);

  zx::port port_;

  fbl::DoublyLinkedList<fbl::RefPtr<TipcObject>> object_list_
      FXL_GUARDED_BY(object_list_lock_);
  fbl::Mutex object_list_lock_;

  FXL_DISALLOW_COPY_AND_ASSIGN(TipcObjectSet);
};

}  // namespace ree_agent
