// Copyright 2018 Open Trust Group
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fbl/unique_ptr.h>
#include <zircon/types.h>
#include <zx/channel.h>

#include "third_party/protobuf/src/google/protobuf/service.h"

using namespace google;

namespace ree_agent {

class RpcChannel : public protobuf::RpcChannel {
 public:
  RpcChannel() = delete;
  RpcChannel(zx::channel channel) : channel_(fbl::move(channel)) {}

  void CallMethod(const protobuf::MethodDescriptor* method,
                  protobuf::RpcController* controller,
                  const protobuf::Message* request, protobuf::Message* response,
                  protobuf::Closure* done) override;

 private:
  zx::channel channel_;
};

}  // namespace ree_agent
