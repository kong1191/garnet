// Copyright 2018 Open Trust Group
// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/ree_agent/cpp/application_context.h"

#include "lib/fxl/logging.h"

namespace ree_agent {

void ApplicationContext::ConnectToEnvironmentService(
    const std::string& interface_name,
    zx::channel channel) {
  return incoming_services().ConnectToService(std::move(channel),
                                              interface_name);
}

}  // namespace ree_agent
