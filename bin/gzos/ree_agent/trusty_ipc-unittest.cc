// Copyright 2018 Open Trust Group
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zircon/compiler.h>

#include "garnet/bin/gzos/ree_agent/trusted_app_fake.h"
#include "garnet/bin/gzos/ree_agent/trusted_app_loader_fake.h"
#include "gtest/gtest.h"

namespace ree_agent {

class TipcAgentTest : public ::testing::Test {
 protected:
  virtual void SetUp() {
  }
};


TEST_F(TipcAgentTest, PortConnect) {
}

}  // namespace reeagent
