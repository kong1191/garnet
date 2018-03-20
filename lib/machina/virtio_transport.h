// Copyright 2018 Open Trust Group. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_MACHINA_VIRTIO_TRANSPORT_H_
#define GARNET_LIB_MACHINA_VIRTIO_TRANSPORT_H_

#include "garnet/lib/machina/io.h"
#include "garnet/lib/machina/phys_mem.h"

namespace machina {

class VirtioDevice;

class VirtioTransport {

 public:
  // Notify remote side for checking queue state
  virtual zx_status_t Notify() = 0;

  void set_device(VirtioDevice *device) { device_ = device; }

 private:
  VirtioDevice* device_;
};

};

#endif
