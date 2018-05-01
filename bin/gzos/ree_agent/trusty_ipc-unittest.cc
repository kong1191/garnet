// Copyright 2018 Open Trust Group
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/cpp/ree_agent.h>
#include <zircon/compiler.h>

#include "garnet/bin/gzos/ree_agent/ree_agent.h"
#include "gtest/gtest.h"
#include "lib/ree_agent/cpp/port.h"

namespace ree_agent {

TEST(TipcAgentTest, PublishPort) {
  async::Loop loop(&kAsyncLoopConfigMakeDefault);
  ReeAgent ree_agent(loop.async());

  // Create and register TipcPortManager service
  TipcPortManagerImpl port_manager;
  ree_agent.AddService<TipcPortManager>(
      [&port_manager](fidl::InterfaceRequest<TipcPortManager> request) {
        port_manager.Bind(fbl::move(request));
      });

  // Grab service directory, this is used by Trusted Application
  // for getting services published by ree_agent
  zx::channel svc_dir = ree_agent.OpenAsDirectory();
  ASSERT_TRUE(svc_dir != ZX_HANDLE_INVALID);

  // Create a Services object for Trusted Application to connect
  // TipcPortManager service
  component::Services services;
  services.Bind(fbl::move(svc_dir));

  // Now publish a test port at Trusted Application
  fbl::String port_path("org.opentrust.test");
  TipcPort test_port(&services, port_path);
  test_port.Publish([](void) {});

  loop.RunUntilIdle();

  // Verify the published port in port talbe
  TipcPortTableEntry* entry;
  ASSERT_EQ(port_manager.Find(port_path, entry), ZX_OK);
  ASSERT_TRUE(entry != nullptr);
  EXPECT_STREQ(entry->path().c_str(), port_path.c_str());
}

}  // namespace ree_agent
