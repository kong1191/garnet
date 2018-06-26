// Copyright 2018 Open Trust Group
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <lib/zx/channel.h>
#include <string>

namespace ree_agent {

void ConnectToTaService(zx::channel request, const std::string& service_name);

}  // namespace ree_agent
