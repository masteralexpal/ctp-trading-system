/**
 * @file thread_test.cc
 * @author lipingan (lipingan.dev@outlook.com)
 * @brief Named thread handle tests.
 */
#include "base/thread.h"

#include <pthread.h>

#include <atomic>
#include <string>

#include <gtest/gtest.h>

namespace cts::base {
namespace {

TEST(ThreadTest, RunsPayloadAndStops) {
  Thread t("test-run");
  std::atomic<bool> ran{false};
  t.Start([&] { ran = true; });
  t.Stop();
  EXPECT_TRUE(ran.load());
  EXPECT_FALSE(t.IsRunning());
}

TEST(ThreadTest, DoubleStartIsNoop) {
  Thread t("test-twice");
  std::atomic<int> invocations{0};
  std::atomic<bool> release{false};
  t.Start([&] {
    ++invocations;
    while (!release.load(std::memory_order_acquire)) {
    }
  });
  t.Start([&] { ++invocations; });  // the second Start must be ignored
  release.store(true);
  t.Stop();
  EXPECT_EQ(invocations.load(), 1);
}

TEST(ThreadTest, StopWithoutStartIsSafe) {
  Thread t("test-idle");
  t.Stop();  // simply must not crash
  SUCCEED();
}

TEST(ThreadTest, NameVisibleInOs) {
  Thread t("md-dispatch");
  std::string os_name;
  t.Start([&] {
    char buf[16] = {};
    pthread_getname_np(pthread_self(), buf, sizeof(buf));
    os_name = buf;
  });
  t.Stop();
  EXPECT_EQ(os_name, "md-dispatch");
}

TEST(ThreadTest, LongNameTruncatedTo15) {
  Thread t("this-name-is-way-too-long");
  std::string os_name;
  t.Start([&] {
    char buf[16] = {};
    pthread_getname_np(pthread_self(), buf, sizeof(buf));
    os_name = buf;
  });
  t.Stop();
  EXPECT_EQ(os_name, "this-name-is-wa");  // first 15 chars
}

TEST(ThreadTest, IsRunningLifecycle) {
  Thread t("test-lifecycle");
  std::atomic<bool> release{false};
  EXPECT_FALSE(t.IsRunning());
  t.Start([&] {
    while (!release.load(std::memory_order_acquire)) {
    }
  });
  EXPECT_TRUE(t.IsRunning());
  release.store(true);
  t.Stop();
  EXPECT_FALSE(t.IsRunning());
}

TEST(ThreadTest, DestructorJoins) {
  std::atomic<bool> finished{false};
  std::atomic<bool> release{false};
  {
    Thread t("test-dtor");
    t.Start([&] {
      while (!release.load(std::memory_order_acquire)) {
      }
      finished = true;
    });
    release.store(true);
  }  // dtor must join: the payload has finished when the scope ends
  EXPECT_TRUE(finished.load());
}

}  // namespace
}  // namespace cts::base
