/**
 * @file no_destructor_test.cc
 * @author lipingan (lipingan.dev@outlook.com)
 * @brief NoDestructor tests.
 */
#include "base/no_destructor.h"

#include <string>

#include <gtest/gtest.h>

namespace cts::base {
namespace {

struct Tracker {
  static inline int alive = 0;
  Tracker() { ++alive; }
  Tracker(Tracker const&) { ++alive; }
  ~Tracker() { --alive; }
};

struct Config {
  std::string name;
  int version;
};

// The canonical function-local static usage
Config& GetStaticConfig() {
  static NoDestructor<Config> cfg("default", 1);
  return *cfg;
}

TEST(NoDestructorTest, ConstructsWithArgsAndAccessors) {
  NoDestructor<Config> cfg("ctp", 7);
  EXPECT_EQ(cfg->name, "ctp");
  EXPECT_EQ((*cfg).version, 7);
  EXPECT_EQ(cfg.get()->name.size(), 3u);
}

TEST(NoDestructorTest, ConstAccess) {
  NoDestructor<std::string> const s("hello");
  EXPECT_EQ(*s, "hello");
  EXPECT_EQ(s->size(), 5u);
}

TEST(NoDestructorTest, StaticInstanceIdentity) {
  Config& a = GetStaticConfig();
  Config& b = GetStaticConfig();
  EXPECT_EQ(&a, &b);  // same instance

  a.version = 42;  // mutation visible across calls
  EXPECT_EQ(GetStaticConfig().version, 42);
}

// Core contract: the destructor NEVER runs (deliberate leak by design)
TEST(NoDestructorTest, NeverRunsDestructor) {
  Tracker::alive = 0;
  {
    NoDestructor<Tracker> t;
    EXPECT_EQ(Tracker::alive, 1);
  }  // NoDestructor is trivially destructed; Tracker never destructs
  EXPECT_EQ(Tracker::alive, 1);
}

// Multi-argument forwarding
TEST(NoDestructorTest, ForwardsMultipleArgs) {
  NoDestructor<std::string> s(5, 'x');
  EXPECT_EQ(*s, "xxxxx");
}

}  // namespace
}  // namespace cts::base
