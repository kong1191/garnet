// Copyright 2018 Open Trust Group
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/public/lib/gzos/ree_agent/cpp/rpc_channel.h"

namespace ree_agent {

void RpcChannel::CallMethod(const protobuf::MethodDescriptor* method,
                            protobuf::RpcController* controller,
                            const protobuf::Message* request,
                            protobuf::Message* response,
                            protobuf::Closure* done) {}

}  // namespace ree_agent
