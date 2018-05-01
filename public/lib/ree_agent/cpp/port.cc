// Copyright 2018 Open Trust Group
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/ree_agent/cpp/port.h"

namespace ree_agent {

void TipcPort::OnConnectionRequest(
    fidl::InterfaceRequest<TipcChannel> channel) {
  callback_();
}

}  // namespace ree_agent
