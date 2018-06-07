// Copyright 2018 Open Trust Group
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "gtest/gtest.h"

#include "lib/ree_agent/cpp/object.h"
#include "lib/ree_agent/cpp/object_manager.h"

namespace ree_agent {

class TipcObjectFake : public TipcObject {
 public:
  // It doesn't matter, just required to instantiate fake TipcObject
  virtual ObjectType get_type() override { return ObjectType(0); }
};

class TipcObjectTest : public ::testing::Test {
 protected:
  TipcObjectTest() : obj_mgr_(TipcObjectManager::Instance()) {}

  void SetUp() {
    object1_ = AdoptRef(new TipcObjectFake());
    ASSERT_TRUE(object1_ != nullptr);
    ASSERT_EQ(obj_mgr_->AddObject(object1_), ZX_OK);

    object2_ = AdoptRef(new TipcObjectFake());
    ASSERT_TRUE(object2_ != nullptr);
    ASSERT_EQ(obj_mgr_->AddObject(object2_), ZX_OK);
  }

  void TearDown() {
    obj_mgr_->RemoveObject(object1_->handle_id());
    obj_mgr_->RemoveObject(object2_->handle_id());
  }

  TipcObjectManager* obj_mgr_;
  fbl::RefPtr<TipcObject> object1_;
  fbl::RefPtr<TipcObject> object2_;
};

TEST_F(TipcObjectTest, WaitObject) {
  EXPECT_EQ(object1_->SignalEvent(TipcEvent::READY), ZX_OK);

  WaitResult result;
  EXPECT_EQ(object1_->Wait(&result, zx::time() + zx::usec(1)), ZX_OK);
  EXPECT_EQ(result.event, TipcEvent::READY);
  EXPECT_EQ(result.handle_id, object1_->handle_id());

  // Test again to make sure event state is cleared
  EXPECT_EQ(object1_->Wait(&result, zx::time() + zx::usec(1)),
            ZX_ERR_TIMED_OUT);

  // The event should not be observed by object set
  EXPECT_EQ(obj_mgr_->obj_set()->Wait(&result, zx::time() + zx::usec(1)),
            ZX_ERR_TIMED_OUT);
}

TEST_F(TipcObjectTest, WaitObjectWithMultipleEvent) {
  EXPECT_EQ(object1_->SignalEvent(TipcEvent::READY), ZX_OK);
  EXPECT_EQ(object1_->SignalEvent(TipcEvent::MSG), ZX_OK);

  WaitResult result;
  EXPECT_EQ(object1_->Wait(&result, zx::time() + zx::usec(1)), ZX_OK);
  EXPECT_EQ(result.event, TipcEvent::READY | TipcEvent::MSG);
  EXPECT_EQ(result.handle_id, object1_->handle_id());
}

TEST_F(TipcObjectTest, WaitObjectSet) {
  EXPECT_EQ(obj_mgr_->obj_set()->AddObject(object1_), ZX_OK);
  EXPECT_EQ(obj_mgr_->obj_set()->AddObject(object2_), ZX_OK);

  EXPECT_EQ(object1_->SignalEvent(TipcEvent::READY), ZX_OK);
  EXPECT_EQ(object2_->SignalEvent(TipcEvent::MSG), ZX_OK);

  WaitResult result;
  EXPECT_EQ(obj_mgr_->obj_set()->Wait(&result, zx::time() + zx::usec(1)),
            ZX_OK);
  EXPECT_EQ(result.handle_id, object1_->handle_id());
  EXPECT_EQ(result.event, TipcEvent::READY);

  EXPECT_EQ(obj_mgr_->obj_set()->Wait(&result, zx::time() + zx::usec(1)),
            ZX_OK);
  EXPECT_EQ(result.event, TipcEvent::MSG);
  EXPECT_EQ(result.handle_id, object2_->handle_id());

  // Test again to make sure event state is cleared
  EXPECT_EQ(obj_mgr_->obj_set()->Wait(&result, zx::time() + zx::usec(1)),
            ZX_ERR_TIMED_OUT);
}

TEST_F(TipcObjectTest, WaitObjectSetWithMultipleEvent) {
  EXPECT_EQ(obj_mgr_->obj_set()->AddObject(object1_), ZX_OK);

  EXPECT_EQ(object1_->SignalEvent(TipcEvent::READY), ZX_OK);
  EXPECT_EQ(object1_->SignalEvent(TipcEvent::MSG), ZX_OK);

  WaitResult result;
  EXPECT_EQ(obj_mgr_->obj_set()->Wait(&result, zx::time() + zx::usec(1)),
            ZX_OK);
  EXPECT_EQ(result.event, TipcEvent::READY | TipcEvent::MSG);
  EXPECT_EQ(result.handle_id, object1_->handle_id());
}

}  // namespace ree_agent
