// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/trusty/tipc_device.h"
#include "lib/fxl/logging.h"

namespace trusty {

TipcDevice::TipcDevice(tipc_vdev_descr* descr) : descr_(*descr) {}

TipcDevice::~TipcDevice() {}

}  // namespace trusty
