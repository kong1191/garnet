// Copyright 2018
// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "lib/svc/cpp/services.h"

using namespace component;

namespace ree_agent {

// Provides access to the application's environment
class ApplicationContext {
 public:
  const Services& incoming_services() const { return incoming_services_; }

  // Connects to a service provided by the application's environment,
  // returning an interface pointer.
  template <typename Interface>
  fidl::InterfacePtr<Interface> ConnectToEnvironmentService(
      const std::string& interface_name = Interface::Name_) {
    return incoming_services().ConnectToService<Interface>(interface_name);
  }

  // Connects to a service provided by the application's environment,
  // binding the service to an interface request.
  template <typename Interface>
  void ConnectToEnvironmentService(
      fidl::InterfaceRequest<Interface> request,
      const std::string& interface_name = Interface::Name_) {
    return incoming_services().ConnectToService(std::move(request),
                                                interface_name);
  }

  // Connects to a service provided by the application's environment,
  // binding the service to a channel.
  void ConnectToEnvironmentService(const std::string& interface_name,
                                   zx::channel channel);

 private:
  Services incoming_services_;
};

}  // namespace ree_agent
