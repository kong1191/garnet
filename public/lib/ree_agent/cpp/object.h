// Copyright 2018 Open Trust Group
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <fbl/auto_lock.h>
#include <fbl/intrusive_double_list.h>
#include <fbl/mutex.h>
#include <fbl/ref_counted.h>
#include <fbl/ref_ptr.h>
#include <ree_agent/cpp/fidl.h>
#include <zx/event.h>
#include <zx/port.h>

#include "lib/fxl/logging.h"
#include "lib/ree_agent/cpp/id_alloc.h"

namespace ree_agent {

enum TipcEvent : zx_signals_t {
  READY = ZX_USER_SIGNAL_0,
  ERROR = ZX_USER_SIGNAL_1,
  HUP = ZX_USER_SIGNAL_2,
  MSG = ZX_USER_SIGNAL_3,
  SEND_UNBLOCKED = ZX_USER_SIGNAL_4,

  ALL = READY | ERROR | HUP | MSG | SEND_UNBLOCKED,
};

struct WaitResult {
  uint32_t handle_id;
  void* cookie;
  zx_signals_t event;
};

class TipcObject : public fbl::RefCounted<TipcObject>,
                   public fbl::DoublyLinkedListable<fbl::RefPtr<TipcObject>> {
 public:
  enum ObjectType { PORT, CHANNEL, OBJECT_SET };
  static constexpr uint32_t kInvalidHandle = UINT_MAX;

  TipcObject();
  virtual ~TipcObject();

  virtual zx_status_t Wait(WaitResult* result, zx::time deadline);

  zx_status_t SignalEvent(uint32_t set_mask) {
    return event_.signal(0x0, set_mask);
  }
  zx_status_t ClearEvent(uint32_t clear_mask) {
    return event_.signal(clear_mask, 0x0);
  }

  zx_status_t BeginAsyncWait(zx::port& port);
  void CancelAsyncWait(zx::port& port);

  bool is_port() { return (get_type() == ObjectType::PORT); }
  bool is_channel() { return (get_type() == ObjectType::CHANNEL); }
  bool is_object_set() { return (get_type() == ObjectType::OBJECT_SET); }

  uint32_t handle_id() const { return handle_id_; }
  void* cookie() { return cookie_; }

 protected:
  virtual ObjectType get_type() = 0;

 private:
  friend class TipcObjectManager;

  uint32_t handle_id_;
  void* cookie_;
  zx::event event_;

  FXL_DISALLOW_COPY_AND_ASSIGN(TipcObject);
};

}  // namespace ree_agent
