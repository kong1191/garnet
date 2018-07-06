// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>
#include "../echo_server/echo_server_app.h"

int main(int argc, const char** argv) {
  async::Loop loop(&kAsyncLoopConfigMakeDefault);
  trusty_ipc::EchoServerApp app;

  loop.StartThread();

  while (true) {
    app.StartService();
  }

  loop.Quit();
  loop.JoinThreads();
  return 0;
}
