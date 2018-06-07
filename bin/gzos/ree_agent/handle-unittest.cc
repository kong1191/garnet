// Copyright 2018 Open Trust Group
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "gtest/gtest.h"

#include "lib/ree_agent/cpp/handle.h"
#include "lib/ree_agent/cpp/object.h"
#include "lib/ree_agent/cpp/object_manager.h"

namespace ree_agent {

class TipcObjectFake : public TipcObject {
 public:
  // It doesn't matter, just required to instantiate fake TipcObject
  virtual ObjectType get_type() override { return ObjectType(0); }
};

class TipcHandleTest : public ::testing::Test {
 protected:
  void SetUp() {
    fbl::unique_ptr<TipcObjectFake> object;
    uint32_t id;

    object.reset(new TipcObjectFake());
    ASSERT_TRUE(object != nullptr);
    ASSERT_EQ(obj_mgr_.CreateHandle(fbl::move(object), &id), Status::OK);
    handle1_ = obj_mgr_.GetHandle(id);
    ASSERT_TRUE(handle1_ != nullptr);

    object.reset(new TipcObjectFake());
    ASSERT_TRUE(object != nullptr);
    ASSERT_EQ(obj_mgr_.CreateHandle(fbl::move(object), &id), Status::OK);
    handle2_ = obj_mgr_.GetHandle(id);
    ASSERT_TRUE(handle2_ != nullptr);
  }

  TipcObjectManager obj_mgr_;
  TipcHandle* handle1_;
  TipcHandle* handle2_;
};

TEST_F(TipcHandleTest, WaitOne) {
  EXPECT_EQ(handle1_->object()->SignalEvent(TipcEvent::READY), ZX_OK);

  zx_signals_t signal;
  EXPECT_EQ(obj_mgr_.WaitOne(handle1_->id(), &signal, zx::time() + zx::usec(1)),
            Status::OK);
  EXPECT_EQ(signal, TipcEvent::READY);

  // Test again to make sure event state is cleared by ObjectManager
  EXPECT_EQ(handle1_->object()->SignalEvent(TipcEvent::MSG), ZX_OK);
  EXPECT_EQ(obj_mgr_.WaitOne(handle1_->id(), &signal, zx::time() + zx::usec(1)),
            Status::OK);
  EXPECT_EQ(signal, TipcEvent::MSG);
}

TEST_F(TipcHandleTest, WaitOneWithMultipleEvent) {
  EXPECT_EQ(handle1_->object()->SignalEvent(TipcEvent::READY), ZX_OK);
  EXPECT_EQ(handle1_->object()->SignalEvent(TipcEvent::MSG), ZX_OK);

  zx_signals_t signal;
  EXPECT_EQ(obj_mgr_.WaitOne(handle1_->id(), &signal, zx::time() + zx::usec(1)),
            Status::OK);
  EXPECT_EQ(signal, TipcEvent::READY | TipcEvent::MSG);
}

TEST_F(TipcHandleTest, WaitOneThenWaitAny) {
  EXPECT_EQ(handle1_->object()->SignalEvent(TipcEvent::READY), ZX_OK);

  zx_signals_t signal;
  EXPECT_EQ(obj_mgr_.WaitOne(handle1_->id(), &signal, zx::time() + zx::usec(1)),
            Status::OK);
  EXPECT_EQ(signal, TipcEvent::READY);

  uint32_t id;
  EXPECT_EQ(obj_mgr_.WaitAny(&id, &signal, zx::time() + zx::usec(1)),
            Status::TIMED_OUT);
}

TEST_F(TipcHandleTest, WaitAny) {
  EXPECT_EQ(handle1_->object()->SignalEvent(TipcEvent::READY), ZX_OK);
  EXPECT_EQ(handle2_->object()->SignalEvent(TipcEvent::MSG), ZX_OK);

  zx_signals_t signal;
  uint32_t id;
  EXPECT_EQ(obj_mgr_.WaitAny(&id, &signal, zx::time() + zx::usec(1)),
            Status::OK);
  EXPECT_EQ(id, handle1_->id());
  EXPECT_EQ(signal, TipcEvent::READY);

  EXPECT_EQ(obj_mgr_.WaitAny(&id, &signal, zx::time() + zx::usec(1)),
            Status::OK);
  EXPECT_EQ(signal, TipcEvent::MSG);
  EXPECT_EQ(id, handle2_->id());

  // Test again to make sure event state is cleared by ObjectManager
  EXPECT_EQ(handle1_->object()->SignalEvent(TipcEvent::MSG), ZX_OK);
  EXPECT_EQ(obj_mgr_.WaitAny(&id, &signal, zx::time() + zx::usec(1)),
            Status::OK);
  EXPECT_EQ(signal, TipcEvent::MSG);
  EXPECT_EQ(id, handle1_->id());
}

TEST_F(TipcHandleTest, WaitAnyWithMultipleEvent) {
  EXPECT_EQ(handle1_->object()->SignalEvent(TipcEvent::READY), ZX_OK);
  EXPECT_EQ(handle1_->object()->SignalEvent(TipcEvent::MSG), ZX_OK);

  uint32_t id;
  zx_signals_t signal;
  EXPECT_EQ(obj_mgr_.WaitAny(&id, &signal, zx::time() + zx::usec(1)),
            Status::OK);
  EXPECT_EQ(signal, TipcEvent::READY | TipcEvent::MSG);
  EXPECT_EQ(id, handle1_->id());
}

TEST_F(TipcHandleTest, WaitAnyThenWaitOne) {
  EXPECT_EQ(handle1_->object()->SignalEvent(TipcEvent::READY), ZX_OK);

  zx_signals_t signal;
  uint32_t id;
  EXPECT_EQ(obj_mgr_.WaitAny(&id, &signal, zx::time() + zx::usec(1)),
            Status::OK);
  EXPECT_EQ(signal, TipcEvent::READY);
  EXPECT_EQ(id, handle1_->id());

  EXPECT_EQ(obj_mgr_.WaitOne(handle1_->id(), &signal, zx::time() + zx::usec(1)),
            Status::TIMED_OUT);
}

}  // namespace ree_agent
