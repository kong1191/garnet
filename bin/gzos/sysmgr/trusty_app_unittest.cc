// Copyright 2018 Open Trust Group
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>

#include "garnet/bin/gzos/sysmgr/trusty_app.h"
#include "gtest/gtest.h"

namespace sysmgr {

class ServiceProviderTest : public ::testing::Test {
 public:
  ServiceProviderTest()
      : loop_(&kAsyncLoopConfigMakeDefault),
        dir_(fbl::AdoptRef(new fs::PseudoDir())),
        provider_(dir_, nullptr) {}

 protected:
  void SetUp() override { provider_.Bind(services_.NewRequest()); }

  async::Loop loop_;
  fbl::RefPtr<fs::PseudoDir> dir_;
  TrustyServiceProvider provider_;
  ServiceProviderPtr services_;
};

TEST_F(ServiceProviderTest, AddService) {
  zx::channel srv, cli;
  ASSERT_EQ(zx::channel::create(0, &srv, &cli), ZX_OK);
  zx_handle_t expected_handle = cli.get();
  zx_handle_t actual_handle;

  services_->AddService("foo", [&actual_handle](zx::channel channel) {
    actual_handle = channel.get();
  });

  services_->ConnectToService("foo", std::move(cli));

  loop_.RunUntilIdle();
  EXPECT_EQ(expected_handle, actual_handle);
}

TEST_F(ServiceProviderTest, WaitOnService) {
  zx::channel srv, cli;
  ASSERT_EQ(zx::channel::create(0, &srv, &cli), ZX_OK);
  zx_handle_t expected_handle = cli.get();
  zx_handle_t actual_handle;

  services_->WaitOnService("foo", [this, &cli] {
    services_->ConnectToService("foo", std::move(cli));
  });

  services_->AddService("foo", [&actual_handle](zx::channel channel) {
    actual_handle = channel.get();
  });

  loop_.RunUntilIdle();
  EXPECT_EQ(expected_handle, actual_handle);
}

TEST_F(ServiceProviderTest, LookupService) {
  bool result;
  services_->LookupService("foo", [&result](bool found) { result = found; });

  loop_.RunUntilIdle();
  EXPECT_FALSE(result);

  services_->AddService("foo", [](zx::channel channel) {});

  services_->LookupService("foo", [&result](bool found) { result = found; });

  loop_.RunUntilIdle();
  EXPECT_TRUE(result);
}

};  // namespace sysmgr
