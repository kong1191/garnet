// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_EXAMPLES_FIDL_ECHO2_SERVER_CPP_ECHO_SERVER_APP_H_
#define GARNET_EXAMPLES_FIDL_ECHO2_SERVER_CPP_ECHO_SERVER_APP_H_

#include "lib/app/cpp/startup_context.h"

#include "lib/gzos/trusty_ipc/cpp/channel.h"
#include "lib/gzos/trusty_ipc/cpp/port.h"

namespace trusty_ipc {

class EchoServerApp {
 public:
  EchoServerApp();
  ~EchoServerApp() {}
  void StartService();

 protected:
  EchoServerApp(std::unique_ptr<fuchsia::sys::StartupContext> context);
  static constexpr uint32_t kNumItems = 3u;
  static constexpr size_t kItemSize = 1024u;

 private:
  EchoServerApp(const EchoServerApp&) = delete;
  EchoServerApp& operator=(const EchoServerApp&) = delete;
  std::unique_ptr<fuchsia::sys::StartupContext> context_;
  TipcPortImpl port_;
};

}  // namespace trusty_ipc

#endif  // GARNET_EXAMPLES_FIDL_ECHO2_SERVER_CPP_ECHO_SERVER_APP_H_
