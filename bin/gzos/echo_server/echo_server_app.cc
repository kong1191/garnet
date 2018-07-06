// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "../echo_server/echo_server_app.h"

#include "lib/app/cpp/startup_context.h"

namespace trusty_ipc {

EchoServerApp::EchoServerApp()
    : EchoServerApp(fuchsia::sys::StartupContext::CreateFromStartupInfo()) {}

EchoServerApp::EchoServerApp(
    std::unique_ptr<fuchsia::sys::StartupContext> context)
    : context_(std::move(context)), port_(kNumItems, kItemSize) {
  std::string service_name("com.android.ipc-unittest.srv.echo");

  context_->outgoing().AddPublicService<TipcPort>(
      [this](fidl::InterfaceRequest<TipcPort> request) {
          port_.Bind(std::move(request));
      }, service_name);
}

void EchoServerApp::StartService() {
  printf("Start echo service\n");

  WaitResult result;
  zx_status_t status = port_.Wait(&result, zx::time::infinite());
  if (status != ZX_OK) {
    printf("error on port wait: %d\n", status);
    return;
  }

  fbl::RefPtr<TipcChannelImpl> ch;
  if (result.event == TipcEvent::READY) {
    printf("receive port ready event\n");
    status = port_.Accept(nullptr, &ch);
    if (status != ZX_OK) {
      printf("error on port accept: %d\n", status);
      return;
    }

    status = ch->Wait(&result, zx::time::infinite());
    if (status != ZX_OK) {
      printf("error on channel wait: %d\n", status);
      return;
    }

    uint32_t msg_id;
    size_t len;
    char buf[128];
    size_t buf_size = 128;
    if (result.event == TipcEvent::MSG) {
      status = ch->GetMessage(&msg_id, &len);
      if (status != ZX_OK) {
        printf("error on get message: %d\n", status);
        return;
      }

      memset(buf, 0, buf_size);
      status = ch->ReadMessage(msg_id, 0, buf, &buf_size);
      if (status != ZX_OK) {
        printf("error on read message: %d\n", status);
        return;
      }

      printf("message come in(size=%zd)\n", buf_size);

      status = ch->PutMessage(msg_id);
      if (status != ZX_OK) {
        printf("error on put message: %d\n", status);
        return;
      }

      printf("echo message back to ree\n");
      status = ch->SendMessage(buf, buf_size);
      if (status != ZX_OK) {
        printf("error on send message: %d\n", status);
        return;
      }
    }
  }

  ch->Shutdown();
}

}  // namespace trusty_ipc
