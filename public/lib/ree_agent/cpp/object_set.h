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

class TipcObjectSet : public TipcObject, public TipcObjectObserver {
 public:
  TipcObjectSet() = default;
  zx_status_t AddObject(fbl::RefPtr<TipcObject> obj);
  void RemoveObject(fbl::RefPtr<TipcObject> obj);

  virtual zx_status_t Wait(WaitResult* result, zx::time deadline) override;

 protected:
  ObjectType get_type() override { return ObjectType::OBJECT_SET; }

 private:
  void OnChildAttached(TipcObjectRef& ref) override;
  void OnChildDetached(TipcObjectRef& ref) override;
  void OnEvent(TipcObjectRef& ref) override;

  bool PollPendingEvents(WaitResult* result);

  void AppendToPendingList(TipcObjectRef& ref);
  void RemoveFromPendingList(TipcObjectRef& ref);

  uint32_t children_count();

  struct PendingListTraits {
    static TipcObjectRef::NodeState& node_state(TipcObjectRef& ref) {
      return ref.pending_list_node;
    }
  };
  using PendingList = fbl::DoublyLinkedList<TipcObjectRef*, PendingListTraits>;

  PendingList pending_list_ FXL_GUARDED_BY(mutex_);
  uint32_t children_count_ FXL_GUARDED_BY(mutex_);
  fbl::Mutex mutex_;
};

}  // namespace ree_agent
