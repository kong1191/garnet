// Copyright 2018 Open Trust Group. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_MACHINA_VIRTIO_TRANSPORT_FAKE_H_
#define GARNET_LIB_MACHINA_VIRTIO_TRANSPORT_FAKE_H_

#include "garnet/lib/machina/virtio_transport.h"

namespace machina {

class VirtioDevice;

class VirtioTransportFake : public VirtioTransport {

 public:
  virtual zx_status_t Notify() { return ZX_OK; }

};

};

#endif

