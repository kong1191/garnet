// Copyright 2018 Open Trust Group
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <fbl/string.h>
#include <fuchsia/cpp/ree_agent.h>

#include "lib/fidl/cpp/binding.h"
#include "lib/fxl/logging.h"
#include "lib/svc/cpp/services.h"

namespace ree_agent {

class TipcPort : public TipcPortListener {
 public:
  using ConnectRequestCallback = std::function<void()>;

  TipcPort(component::Services* services, const fbl::String path)
      : binding_(this), path_(path.c_str()) {
    FXL_LOG(ERROR) << "before connect";
    services->ConnectToService<TipcPortManager>(port_mgr_.NewRequest());
    FXL_LOG(ERROR) << "after connect";
  }

  void Publish(ConnectRequestCallback callback) {
    callback_ = callback;

    fidl::InterfaceHandle<TipcPortListener> handle;
    binding_.Bind(handle.NewRequest());
    port_mgr_->Publish(path_, std::move(handle));
  }

  ~TipcPort() {
    // TODO(sy): unregister port here
  }

 private:
  TipcPort();
  void OnConnectionRequest(
      fidl::InterfaceRequest<TipcChannel> channel) override;

  fidl::Binding<TipcPortListener> binding_;
  fidl::StringPtr path_;
  fidl::InterfacePtr<TipcPortManager> port_mgr_;
  ConnectRequestCallback callback_;

  FXL_DISALLOW_COPY_AND_ASSIGN(TipcPort);
};

}  // namespace ree_agent
