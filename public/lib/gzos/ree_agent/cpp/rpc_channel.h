// Copyright 2018 Open Trust Group
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zircon/types.h>

#include "third_party/protobuf/src/google/protobuf/service.h"

namespace ree_agent {

using namespace google::protobuf;

class GzosChannelBase : public RpcChannel {
 public:
  GzosChannelBase() = delete;
  GzosChannelBase(std::string service_name) : service_name_(service_name) {}
  virtual ~GzosChannelBase() = default;

  void CallMethod(const MethodDescriptor* method, RpcController* controller,
                  const Message* request, Message* response,
                  Closure* done) override;

 private:
  virtual zx_status_t WriteMessage(void* msg, size_t msg_size) = 0;
  virtual zx_status_t ReadMessage(void* msg, size_t* msg_size) = 0;

  std::string service_name_;
};

}  // namespace ree_agent
