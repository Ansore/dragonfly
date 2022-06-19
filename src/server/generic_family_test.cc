// Copyright 2022, Roman Gershman.  All rights reserved.
// See LICENSE for licensing terms.
//

#include "server/generic_family.h"

#include "base/gtest.h"
#include "base/logging.h"
#include "facade/facade_test.h"
#include "server/command_registry.h"
#include "server/conn_context.h"
#include "server/engine_shard_set.h"
#include "server/string_family.h"
#include "server/test_utils.h"
#include "server/transaction.h"
#include "util/uring/uring_pool.h"

using namespace testing;
using namespace std;
using namespace util;
using namespace boost;
using absl::StrCat;

namespace dfly {

class GenericFamilyTest : public BaseFamilyTest {};

TEST_F(GenericFamilyTest, Expire) {
  Run({"set", "key", "val"});
  auto resp = Run({"expire", "key", "1"});

  EXPECT_THAT(resp, IntArg(1));
  UpdateTime(expire_now_ + 1000);
  resp = Run({"get", "key"});
  EXPECT_THAT(resp, ArgType(RespExpr::NIL));

  Run({"set", "key", "val"});
  resp = Run({"pexpireat", "key", absl::StrCat(expire_now_ + 2000)});
  EXPECT_THAT(resp, IntArg(1));

  // override
  resp = Run({"pexpireat", "key", absl::StrCat(expire_now_ + 3000)});
  EXPECT_THAT(resp, IntArg(1));

  UpdateTime(expire_now_ + 2999);
  resp = Run({"get", "key"});
  EXPECT_THAT(resp, "val");

  UpdateTime(expire_now_ + 3000);
  resp = Run({"get", "key"});
  EXPECT_THAT(resp, ArgType(RespExpr::NIL));
}

TEST_F(GenericFamilyTest, Del) {
  for (size_t i = 0; i < 1000; ++i) {
    Run({"set", StrCat("foo", i), "1"});
    Run({"set", StrCat("bar", i), "1"});
  }

  ASSERT_EQ(2000, CheckedInt({"dbsize"}));

  auto exist_fb = pp_->at(0)->LaunchFiber([&] {
    for (size_t i = 0; i < 1000; ++i) {
      int64_t resp = CheckedInt({"exists", StrCat("foo", i), StrCat("bar", i)});
      ASSERT_TRUE(2 == resp || resp == 0) << resp << " " << i;
    }
  });

  auto del_fb = pp_->at(2)->LaunchFiber([&] {
    for (size_t i = 0; i < 1000; ++i) {
      auto resp = CheckedInt({"del", StrCat("foo", i), StrCat("bar", i)});
      ASSERT_EQ(2, resp);
    }
  });

  exist_fb.join();
  del_fb.join();
}

TEST_F(GenericFamilyTest, TTL) {
  EXPECT_EQ(-2, CheckedInt({"ttl", "foo"}));
  EXPECT_EQ(-2, CheckedInt({"pttl", "foo"}));
  Run({"set", "foo", "bar"});
  EXPECT_EQ(-1, CheckedInt({"ttl", "foo"}));
  EXPECT_EQ(-1, CheckedInt({"pttl", "foo"}));
}

TEST_F(GenericFamilyTest, Exists) {
  Run({"mset", "x", "0", "y", "1"});
  auto resp = Run({"exists", "x", "y", "x"});
  EXPECT_THAT(resp, IntArg(3));
}

TEST_F(GenericFamilyTest, Rename) {
  RespExpr resp;
  string b_val(32, 'b');
  string x_val(32, 'x');

  resp = Run({"mset", "x", x_val, "b", b_val});
  ASSERT_EQ(resp, "OK");
  ASSERT_EQ(2, last_cmd_dbg_info_.shards_count);

  resp = Run({"rename", "z", "b"});
  ASSERT_THAT(resp, ErrArg("no such key"));

  resp = Run({"rename", "x", "b"});
  ASSERT_EQ(resp, "OK");

  int64_t val = CheckedInt({"get", "x"});
  ASSERT_EQ(kint64min, val);  // does not exist

  ASSERT_EQ(Run({"get", "b"}), x_val);  // swapped.

  EXPECT_EQ(CheckedInt({"exists", "x", "b"}), 1);

  const char* keys[2] = {"b", "x"};
  auto ren_fb = pp_->at(0)->LaunchFiber([&] {
    for (size_t i = 0; i < 200; ++i) {
      int j = i % 2;
      auto resp = Run({"rename", keys[j], keys[1 - j]});
      ASSERT_EQ(resp, "OK");
    }
  });

  auto exist_fb = pp_->at(2)->LaunchFiber([&] {
    for (size_t i = 0; i < 300; ++i) {
      int64_t resp = CheckedInt({"exists", "x", "b"});
      ASSERT_EQ(1, resp);
    }
  });

  exist_fb.join();
  ren_fb.join();
}

TEST_F(GenericFamilyTest, RenameNonString) {
  EXPECT_EQ(1, CheckedInt({"lpush", "x", "elem"}));
  auto resp = Run({"rename", "x", "b"});
  ASSERT_EQ(resp, "OK");
  ASSERT_EQ(2, last_cmd_dbg_info_.shards_count);

  EXPECT_EQ(0, CheckedInt({"del", "x"}));
  EXPECT_EQ(1, CheckedInt({"del", "b"}));
}

TEST_F(GenericFamilyTest, RenameBinary) {
  const char kKey1[] = "\x01\x02\x03\x04";
  const char kKey2[] = "\x05\x06\x07\x08";

  Run({"set", kKey1, "bar"});
  Run({"rename", kKey1, kKey2});
  EXPECT_THAT(Run({"get", kKey1}), ArgType(RespExpr::NIL));
  EXPECT_EQ(Run({"get", kKey2}), "bar");
}

using testing::AnyOf;
using testing::Each;
using testing::StartsWith;

TEST_F(GenericFamilyTest, Scan) {
  for (unsigned i = 0; i < 10; ++i)
    Run({"set", absl::StrCat("key", i), "bar"});

  for (unsigned i = 0; i < 10; ++i)
    Run({"set", absl::StrCat("str", i), "bar"});

  for (unsigned i = 0; i < 10; ++i)
    Run({"sadd", absl::StrCat("set", i), "bar"});

  for (unsigned i = 0; i < 10; ++i)
    Run({"zadd", absl::StrCat("zset", i), "0", "bar"});

  auto resp = Run({"scan", "0", "count", "20", "type", "string"});
  EXPECT_THAT(resp, ArrLen(2));
  auto vec = StrArray(resp.GetVec()[1]);
  EXPECT_GT(vec.size(), 10);
  EXPECT_THAT(vec, Each(AnyOf(StartsWith("str"), StartsWith("key"))));

  resp = Run({"scan", "0", "count", "20", "match", "zset*"});
  vec = StrArray(resp.GetVec()[1]);
  EXPECT_EQ(10, vec.size());
  EXPECT_THAT(vec, Each(StartsWith("zset")));
}

TEST_F(GenericFamilyTest, Sort) {
  // auto resp = Run({"sort", "mylist"});
  // ASSERT_THAT(resp, ArrLen(5));
  // EXPECT_THAT(resp.GetVec(), ElementsAre("0", "2", "3", "4", "9"));

  // Run({"lpush", "mylist2", "1", "dda", "ee", "ad", "pps"});
  // resp = Run({"sort", "mylist2", "alpha"});
  // ASSERT_THAT(resp, ArrLen(5));
  // EXPECT_THAT(resp.GetVec(), ElementsAre("1", "ad", "dda", "ee", "pps"));
  //
  // resp = Run({"sort", "mylist2", "alpha", "limit", "1", "2"});
  // ASSERT_THAT(resp, ArrLen(2));
  // EXPECT_THAT(resp.GetVec(), ElementsAre("ad", "dda"));
  //
  // Run({"lpush", "mylist3", "1.1", "3.3", "2.2", "5.5", "4.4"});
  // resp = Run({"sort", "mylist3"});
  // ASSERT_THAT(resp, ArrLen(5));
  // EXPECT_THAT(resp.GetVec(), ElementsAre("1.1", "2.2", "3.3", "4.4", "5.5"));
  //
  // resp = Run({"sort", "mylist3", "by", "not-exists-key"});
  // ASSERT_THAT(resp, ArrLen(5));
  // EXPECT_THAT(resp.GetVec(), ElementsAre("4.4", "5.5", "2.2", "3.3", "1.1"));

  Run({"lpush", "mylist", "4", "2", "3", "9", "0"});
  EXPECT_EQ(Run({"set", "ttt_3", "6"}), "OK");
  EXPECT_EQ(Run({"set", "ttt_4", "5"}), "OK");
  EXPECT_EQ(Run({"set", "ttt_9", "0"}), "OK");
  EXPECT_EQ(Run({"set", "ttt_0", "9"}), "OK");
  EXPECT_EQ(Run({"set", "ttt_2", "7"}), "OK");
  EXPECT_EQ(Run({"get", "ttt_0"}), "9");
  EXPECT_EQ(Run({"get", "ttt_2"}), "7");
  EXPECT_EQ(Run({"get", "ttt_3"}), "6");
  EXPECT_EQ(Run({"get", "ttt_4"}), "5");
  EXPECT_EQ(Run({"get", "ttt_9"}), "0");
  auto resp = Run({"sort", "mylist", "by", "ttt_*"});
  ASSERT_THAT(resp, ArrLen(5));
  EXPECT_THAT(resp.GetVec(), ElementsAre("9", "4", "3", "2", "0"));
  // resp = Run({"sort", "mylist", "by", "cwass*"});
}

}  // namespace dfly
