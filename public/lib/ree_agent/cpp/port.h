// Copyright 2018 Open Trust Group
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <fbl/intrusive_single_list.h>
#include <fbl/string.h>
#include <ree_agent/cpp/fidl.h>

#include "lib/fidl/cpp/binding.h"
#include "lib/fxl/logging.h"
#include "lib/ree_agent/cpp/object.h"
#include "lib/svc/cpp/services.h"

namespace ree_agent {

class TipcChannelImpl;

class TipcPortImpl : public TipcPort, public TipcObject {
 public:
  TipcPortImpl(component::Services* services, const TipcPortInfo info)
      : binding_(this),
        path_(info.path->c_str()),
        num_items_(info.num_items),
        item_size_(info.item_size) {
    services->ConnectToService<TipcPortManager>(port_mgr_.NewRequest());
  }
  TipcPortImpl() = delete;

  zx_status_t Publish();
  zx_status_t Accept(fbl::RefPtr<TipcChannelImpl>* channel_out);

  ~TipcPortImpl() {
    // TODO(sy): unregister port here
  }

  ObjectType get_type() override { return ObjectType::PORT; }

 protected:
  void Connect(fidl::InterfaceHandle<TipcChannel> peer_handle,
               ConnectCallback callback) override;

 private:
  fidl::Binding<TipcPort> binding_;
  fidl::StringPtr path_;
  TipcPortManagerSyncPtr port_mgr_;

  uint32_t num_items_;
  size_t item_size_;

  fbl::SinglyLinkedList<fbl::RefPtr<TipcChannelImpl>> pending_requests_;
};

}  // namespace ree_agent
