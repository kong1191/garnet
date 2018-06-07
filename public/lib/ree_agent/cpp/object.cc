// Copyright 2018 Open Trust Group
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/ree_agent/cpp/object.h"

namespace ree_agent {

TipcObject::TipcObject() { FXL_CHECK(zx::event::create(0, &event_) == ZX_OK); }

}  // namespace ree_agent
