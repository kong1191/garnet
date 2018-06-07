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
    object1_.reset(new TipcObjectFake());
    ASSERT_TRUE(object1_ != nullptr);
    ASSERT_EQ(object1_->Init(), ZX_OK);

    object2_.reset(new TipcObjectFake());
    ASSERT_TRUE(object2_ != nullptr);
    ASSERT_EQ(object2_->Init(), ZX_OK);
  }

  TipcObjectManager* obj_mgr_;
  fbl::unique_ptr<TipcObject> object1_;
  fbl::unique_ptr<TipcObject> object2_;
};

TEST_F(TipcObjectTest, WaitOne) {
  EXPECT_EQ(object1_->SignalEvent(TipcEvent::READY), ZX_OK);

  zx_signals_t signal;
  EXPECT_EQ(object1_->Wait(&signal, zx::time() + zx::usec(1)), ZX_OK);
  EXPECT_EQ(signal, TipcEvent::READY);

  // Test again to make sure event state is cleared by ObjectManager
  EXPECT_EQ(object1_->Wait(&signal, zx::time() + zx::usec(1)),
            ZX_ERR_TIMED_OUT);
}

TEST_F(TipcObjectTest, WaitOneWithMultipleEvent) {
  EXPECT_EQ(object1_->SignalEvent(TipcEvent::READY), ZX_OK);
  EXPECT_EQ(object1_->SignalEvent(TipcEvent::MSG), ZX_OK);

  zx_signals_t signal;
  EXPECT_EQ(object1_->Wait(&signal, zx::time() + zx::usec(1)), ZX_OK);
  EXPECT_EQ(signal, TipcEvent::READY | TipcEvent::MSG);
}

TEST_F(TipcObjectTest, WaitAny) {
  uint32_t expected_id1 = object1_->id();
  uint32_t expected_id2 = object2_->id();

  EXPECT_EQ(object1_->SignalEvent(TipcEvent::READY), ZX_OK);
  EXPECT_EQ(object2_->SignalEvent(TipcEvent::MSG), ZX_OK);

  EXPECT_EQ(obj_mgr_->AddObject(fbl::move(object1_)), ZX_OK);
  EXPECT_EQ(obj_mgr_->AddObject(fbl::move(object2_)), ZX_OK);

  zx_signals_t signal;
  uint32_t id;
  EXPECT_EQ(obj_mgr_->Wait(&id, &signal, zx::time() + zx::usec(1)), ZX_OK);
  EXPECT_EQ(id, expected_id1);
  EXPECT_EQ(signal, TipcEvent::READY);

  EXPECT_EQ(obj_mgr_->Wait(&id, &signal, zx::time() + zx::usec(1)), ZX_OK);
  EXPECT_EQ(signal, TipcEvent::MSG);
  EXPECT_EQ(id, expected_id2);
}

TEST_F(TipcObjectTest, WaitAnyWithMultipleEvent) {
  uint32_t expected_id = object1_->id();

  EXPECT_EQ(object1_->SignalEvent(TipcEvent::READY), ZX_OK);
  EXPECT_EQ(object1_->SignalEvent(TipcEvent::MSG), ZX_OK);

  EXPECT_EQ(obj_mgr_->AddObject(fbl::move(object1_)), ZX_OK);

  uint32_t id;
  zx_signals_t signal;
  EXPECT_EQ(obj_mgr_->Wait(&id, &signal, zx::time() + zx::usec(1)), ZX_OK);
  EXPECT_EQ(signal, TipcEvent::READY | TipcEvent::MSG);
  EXPECT_EQ(id, expected_id);

  // Test again to make sure event state is cleared by ObjectManager
  EXPECT_EQ(obj_mgr_->Wait(&id, &signal, zx::time() + zx::usec(1)),
            ZX_ERR_TIMED_OUT);
}

}  // namespace ree_agent
