// Copyright 2018 Open Trust Group
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <fbl/auto_lock.h>
#include <fbl/intrusive_double_list.h>
#include <fbl/mutex.h>
#include <fbl/ref_ptr.h>
#include <ree_agent/cpp/fidl.h>
#include <zx/port.h>

#include "lib/fxl/logging.h"
#include "lib/fxl/synchronization/thread_annotations.h"
#include "lib/ree_agent/cpp/object.h"
namespace ree_agent {

class TipcObjectSet : public TipcObject {
 public:
  TipcObjectSet() = default;
  zx_status_t AddObject(fbl::RefPtr<TipcObject> obj);
  void RemoveObject(fbl::RefPtr<TipcObject> obj);

  zx_status_t AppendToPendingList(TipcObject* obj);
  void RemoveFromPendingList(uint32_t handle_id);

  virtual zx_status_t Wait(WaitResult* result, zx::time deadline) override;

 private:
  bool PollPendingEvents(WaitResult* result);

  ObjectType get_type() override { return ObjectType::OBJECT_SET; }

  TipcObjectRefList pending_list_ FXL_GUARDED_BY(pending_list_lock_);
  fbl::Mutex pending_list_lock_;
};

}  // namespace ree_agent
