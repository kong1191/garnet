// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <filesystem>
#include <map>

#include "garnet/bin/zxdb/client/remote_api.h"
#include "garnet/bin/zxdb/client/session.h"
#include "garnet/bin/zxdb/common/host_util.h"
#include "garnet/lib/debug_ipc/helper/platform_message_loop.h"
#include "gtest/gtest.h"

namespace zxdb {

class MinidumpTest : public testing::Test {
 public:
  MinidumpTest();
  virtual ~MinidumpTest();

  debug_ipc::PlatformMessageLoop& loop() { return loop_; }
  Session& session() { return *session_; }

  Err TryOpen(const std::string& filename);

  template <typename RequestType, typename ReplyType>
  void DoRequest(
      RequestType request, ReplyType& reply, Err& err,
      void (RemoteAPI::*handler)(const RequestType&,
                                 std::function<void(const Err&, ReplyType)>));

 private:
  debug_ipc::PlatformMessageLoop loop_;
  std::unique_ptr<Session> session_;
};

MinidumpTest::MinidumpTest() {
  loop_.Init();
  session_ = std::make_unique<Session>();
}

MinidumpTest::~MinidumpTest() { loop_.Cleanup(); }

Err MinidumpTest::TryOpen(const std::string& filename) {
  static auto data_dir = std::filesystem::path(GetSelfPath())
    .parent_path().parent_path() / "test_data" / "zxdb";

  Err err;
  auto path = (data_dir / filename).string();

  session().OpenMinidump(path,
                         [&err](const Err& got) {
                           err = got;
                           debug_ipc::MessageLoop::Current()->QuitNow();
                         });

  loop().Run();

  return err;
}

template<typename RequestType, typename ReplyType>
void MinidumpTest::DoRequest(RequestType request, ReplyType& reply, Err& err,
                             void (RemoteAPI::*handler)(
                               const RequestType&,
                               std::function<void(const Err&, ReplyType)>)) {
  (session().remote_api()->*handler)(request,
    [&reply, &err](const Err& e, ReplyType r) {
      err = e;
      reply = r;
      debug_ipc::MessageLoop::Current()->QuitNow();
    });
  loop().Run();
}

template<typename Data>
std::vector<uint8_t> AsData(Data d) {
  std::vector<uint8_t> ret;

  ret.resize(sizeof(d));

  *reinterpret_cast<Data*>(ret.data()) = d;
  return ret;
}

#define EXPECT_ZXDB_SUCCESS(e_) \
  ({ Err e = e_; EXPECT_FALSE(e.has_error()) << e.msg(); })
#define ASSERT_ZXDB_SUCCESS(e_) \
  ({ Err e = e_; ASSERT_FALSE(e.has_error()) << e.msg(); })

constexpr uint32_t kTestExampleMinidumpKOID = 656254UL;
constexpr uint32_t kTestExampleMinidumpThreadKOID = 671806UL;

TEST_F(MinidumpTest, Load) {
  EXPECT_ZXDB_SUCCESS(TryOpen("test_example_minidump.dmp"));
}

TEST_F(MinidumpTest, ProcessTreeRecord) {
  ASSERT_ZXDB_SUCCESS(TryOpen("test_example_minidump.dmp"));

  Err err;
  debug_ipc::ProcessTreeReply reply;
  DoRequest(debug_ipc::ProcessTreeRequest(), reply, err,
            &RemoteAPI::ProcessTree);
  ASSERT_ZXDB_SUCCESS(err);

  auto record = reply.root;
  EXPECT_EQ(debug_ipc::ProcessTreeRecord::Type::kProcess, record.type);
  EXPECT_EQ("<core dump>", record.name);
  EXPECT_EQ(kTestExampleMinidumpKOID, record.koid);
  EXPECT_EQ(0UL, record.children.size());
}

TEST_F(MinidumpTest, AttachDetach) {
  ASSERT_ZXDB_SUCCESS(TryOpen("test_example_minidump.dmp"));

  Err err;
  debug_ipc::AttachRequest request;
  debug_ipc::AttachReply reply;

  request.koid = kTestExampleMinidumpKOID;
  DoRequest(request, reply, err, &RemoteAPI::Attach);
  ASSERT_ZXDB_SUCCESS(err);

  EXPECT_EQ(0UL, reply.status);
  EXPECT_EQ("<core dump>", reply.process_name);

  debug_ipc::DetachRequest detach_request;
  debug_ipc::DetachReply detach_reply;

  detach_request.process_koid = kTestExampleMinidumpKOID;
  DoRequest(detach_request, detach_reply, err, &RemoteAPI::Detach);
  ASSERT_ZXDB_SUCCESS(err);

  EXPECT_EQ(0UL, detach_reply.status);

  /* Try to detach when not attached */
  DoRequest(detach_request, detach_reply, err, &RemoteAPI::Detach);
  ASSERT_ZXDB_SUCCESS(err);

  EXPECT_NE(0UL, detach_reply.status);
}

TEST_F(MinidumpTest, AttachFail) {
  ASSERT_ZXDB_SUCCESS(TryOpen("test_example_minidump.dmp"));

  Err err;
  debug_ipc::AttachRequest request;
  debug_ipc::AttachReply reply;

  request.koid = 42;
  DoRequest(request, reply, err, &RemoteAPI::Attach);
  ASSERT_ZXDB_SUCCESS(err);

  EXPECT_NE(0UL, reply.status);
}

TEST_F(MinidumpTest, Threads) {
  ASSERT_ZXDB_SUCCESS(TryOpen("test_example_minidump.dmp"));

  Err err;
  debug_ipc::ThreadsRequest request;
  debug_ipc::ThreadsReply reply;

  request.process_koid = kTestExampleMinidumpKOID;
  DoRequest(request, reply, err, &RemoteAPI::Threads);
  ASSERT_ZXDB_SUCCESS(err);

  ASSERT_LT(0UL, reply.threads.size());
  EXPECT_EQ(1UL, reply.threads.size());

  auto& thread = reply.threads[0];

  EXPECT_EQ(kTestExampleMinidumpThreadKOID, thread.koid);
  EXPECT_EQ("", thread.name);
  EXPECT_EQ(debug_ipc::ThreadRecord::State::kDead, thread.state);
}

TEST_F(MinidumpTest, Registers) {
  ASSERT_ZXDB_SUCCESS(TryOpen("test_example_minidump.dmp"));

  Err err;
  debug_ipc::RegistersRequest request;
  debug_ipc::RegistersReply reply;

  using C = debug_ipc::RegisterCategory::Type;
  using R = debug_ipc::RegisterID;

  request.process_koid = kTestExampleMinidumpKOID;
  request.thread_koid = kTestExampleMinidumpThreadKOID;
  request.categories = {
    C::kGeneral,
    C::kFloatingPoint,
    C::kVector,
    C::kDebug,
  };
  DoRequest(request, reply, err, &RemoteAPI::Registers);
  ASSERT_ZXDB_SUCCESS(err);

  EXPECT_EQ(4UL, reply.categories.size());

  EXPECT_EQ(C::kGeneral, reply.categories[0].type);
  EXPECT_EQ(C::kFloatingPoint, reply.categories[1].type);
  EXPECT_EQ(C::kVector, reply.categories[2].type);
  EXPECT_EQ(C::kDebug, reply.categories[3].type);

  std::map<std::pair<C, R>, std::vector<uint8_t>> got;
  for (const auto& cat : reply.categories) {
    for (const auto& reg : cat.registers) {
      got[std::pair(cat.type, reg.id)] = reg.data;
    }
  }

  std::vector<uint8_t> zero_short = { 0, 0 };
  std::vector<uint8_t> zero_128 = { 0, 0, 0, 0, 0, 0, 0, 0,
                                    0, 0, 0, 0, 0, 0, 0, 0 };

  EXPECT_EQ(AsData(0x83UL), got[std::pair(C::kGeneral, R::kX64_rax)]);
  EXPECT_EQ(AsData(0x2FE150062100UL), got[std::pair(C::kGeneral, R::kX64_rbx)]);
  EXPECT_EQ(AsData(0x0UL), got[std::pair(C::kGeneral, R::kX64_rcx)]);
  EXPECT_EQ(AsData(0x4DC647A67264UL), got[std::pair(C::kGeneral, R::kX64_rdx)]);
  EXPECT_EQ(AsData(0x5283B9A79945UL), got[std::pair(C::kGeneral, R::kX64_rsi)]);
  EXPECT_EQ(AsData(0x4DC647A671D8UL), got[std::pair(C::kGeneral, R::kX64_rdi)]);
  EXPECT_EQ(AsData(0x37F880986D70UL), got[std::pair(C::kGeneral, R::kX64_rbp)]);
  EXPECT_EQ(AsData(0x37F880986D48UL), got[std::pair(C::kGeneral, R::kX64_rsp)]);
  EXPECT_EQ(AsData(0x1UL), got[std::pair(C::kGeneral, R::kX64_r8)]);
  EXPECT_EQ(AsData(0x0UL), got[std::pair(C::kGeneral, R::kX64_r9)]);
  EXPECT_EQ(AsData(0x4DC647A671D8UL), got[std::pair(C::kGeneral, R::kX64_r10)]);
  EXPECT_EQ(AsData(0x83UL), got[std::pair(C::kGeneral, R::kX64_r11)]);
  EXPECT_EQ(AsData(0x2FE150077070UL), got[std::pair(C::kGeneral, R::kX64_r12)]);
  EXPECT_EQ(AsData(0x3F4C20970A28UL), got[std::pair(C::kGeneral, R::kX64_r13)]);
  EXPECT_EQ(AsData(0xFFFFFFF5UL), got[std::pair(C::kGeneral, R::kX64_r14)]);
  EXPECT_EQ(AsData(0x2FE150062138UL), got[std::pair(C::kGeneral, R::kX64_r15)]);
  EXPECT_EQ(AsData(0x4DC6479A5B1EUL), got[std::pair(C::kGeneral, R::kX64_rip)]);
  EXPECT_EQ(AsData(0x10206UL), got[std::pair(C::kGeneral, R::kX64_rflags)]);

  EXPECT_EQ(zero_short, got[std::pair(C::kFloatingPoint, R::kX64_fcw)]);
  EXPECT_EQ(zero_short, got[std::pair(C::kFloatingPoint, R::kX64_fsw)]);
  EXPECT_EQ(AsData('\0'), got[std::pair(C::kFloatingPoint, R::kX64_ftw)]);
  EXPECT_EQ(zero_short, got[std::pair(C::kFloatingPoint, R::kX64_fop)]);
  EXPECT_EQ(AsData(0x0UL), got[std::pair(C::kFloatingPoint, R::kX64_fip)]);
  EXPECT_EQ(AsData(0x0UL), got[std::pair(C::kFloatingPoint, R::kX64_fdp)]);
  EXPECT_EQ(zero_128, got[std::pair(C::kFloatingPoint, R::kX64_st0)]);
  EXPECT_EQ(zero_128, got[std::pair(C::kFloatingPoint, R::kX64_st1)]);
  EXPECT_EQ(zero_128, got[std::pair(C::kFloatingPoint, R::kX64_st2)]);
  EXPECT_EQ(zero_128, got[std::pair(C::kFloatingPoint, R::kX64_st3)]);
  EXPECT_EQ(zero_128, got[std::pair(C::kFloatingPoint, R::kX64_st4)]);
  EXPECT_EQ(zero_128, got[std::pair(C::kFloatingPoint, R::kX64_st5)]);
  EXPECT_EQ(zero_128, got[std::pair(C::kFloatingPoint, R::kX64_st6)]);
  EXPECT_EQ(zero_128, got[std::pair(C::kFloatingPoint, R::kX64_st7)]);

  EXPECT_EQ(AsData(0x0U), got[std::pair(C::kVector, R::kX64_mxcsr)]);
  EXPECT_EQ(zero_128, got[std::pair(C::kVector, R::kX64_xmm0)]);
  EXPECT_EQ(zero_128, got[std::pair(C::kVector, R::kX64_xmm1)]);
  EXPECT_EQ(zero_128, got[std::pair(C::kVector, R::kX64_xmm2)]);
  EXPECT_EQ(zero_128, got[std::pair(C::kVector, R::kX64_xmm3)]);
  EXPECT_EQ(zero_128, got[std::pair(C::kVector, R::kX64_xmm4)]);
  EXPECT_EQ(zero_128, got[std::pair(C::kVector, R::kX64_xmm5)]);
  EXPECT_EQ(zero_128, got[std::pair(C::kVector, R::kX64_xmm6)]);
  EXPECT_EQ(zero_128, got[std::pair(C::kVector, R::kX64_xmm7)]);
  EXPECT_EQ(zero_128, got[std::pair(C::kVector, R::kX64_xmm8)]);
  EXPECT_EQ(zero_128, got[std::pair(C::kVector, R::kX64_xmm9)]);
  EXPECT_EQ(zero_128, got[std::pair(C::kVector, R::kX64_xmm10)]);
  EXPECT_EQ(zero_128, got[std::pair(C::kVector, R::kX64_xmm11)]);
  EXPECT_EQ(zero_128, got[std::pair(C::kVector, R::kX64_xmm12)]);
  EXPECT_EQ(zero_128, got[std::pair(C::kVector, R::kX64_xmm13)]);
  EXPECT_EQ(zero_128, got[std::pair(C::kVector, R::kX64_xmm14)]);
  EXPECT_EQ(zero_128, got[std::pair(C::kVector, R::kX64_xmm15)]);

  EXPECT_EQ(AsData(0x0UL), got[std::pair(C::kDebug, R::kX64_dr0)]);
  EXPECT_EQ(AsData(0x0UL), got[std::pair(C::kDebug, R::kX64_dr1)]);
  EXPECT_EQ(AsData(0x0UL), got[std::pair(C::kDebug, R::kX64_dr2)]);
  EXPECT_EQ(AsData(0x0UL), got[std::pair(C::kDebug, R::kX64_dr3)]);
  EXPECT_EQ(AsData(0x0UL), got[std::pair(C::kDebug, R::kX64_dr6)]);
  EXPECT_EQ(AsData(0x0UL), got[std::pair(C::kDebug, R::kX64_dr7)]);
}

}  // namespace zxdb
