// Copyright 2018 Open Trust Group.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>
#include <zircon/process.h>
#include <zircon/processargs.h>

#include "garnet/bin/gzos/ree_agent/ree_message_impl.h"

#include "lib/svc/cpp/services.h"

component::Services service_provider;

namespace ree_agent {

void ConnectToTaService(zx::channel request, const std::string& service_name) {
  service_provider.ConnectToService(std::move(request), service_name);
}

}  // namespace ree_agent

static zx_status_t get_ree_agent_server_channel(zx::channel* out_ch) {
  zx_handle_t request = zx_get_startup_handle(PA_HND(PA_USER0, 0));
  if (request == ZX_HANDLE_INVALID) {
    FXL_LOG(ERROR) << "Can not get ree_agent request server channel";
    return ZX_ERR_NO_RESOURCES;
  }

  out_ch->reset(request);
  return ZX_OK;
}

static zx_status_t get_appmgr_client_channel(zx::channel* out_ch) {
  zx_handle_t request = zx_get_startup_handle(PA_HND(PA_USER0, 1));
  if (request == ZX_HANDLE_INVALID) {
    FXL_LOG(ERROR) << "Can not get appmgr request client channel";
    return ZX_ERR_NO_RESOURCES;
  }

  out_ch->reset(request);
  return ZX_OK;
}

int main(int argc, const char** argv) {
  async::Loop loop(&kAsyncLoopConfigMakeDefault);

  zx::channel appmgr_cli;
  zx_status_t status = get_appmgr_client_channel(&appmgr_cli);
  if (status != ZX_OK)
    return 1;

  service_provider.Bind(std::move(appmgr_cli));

  zx::channel ree_agent_srv;
  status = get_ree_agent_server_channel(&ree_agent_srv);
  if (status != ZX_OK)
    return 1;

  ree_agent::ReeMessageImpl ree_message_impl;
  ree_message_impl.Bind(std::move(ree_agent_srv));

  loop.Run();
  return 0;
}
