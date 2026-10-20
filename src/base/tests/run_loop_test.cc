/**
 * @file run_loop_test.cc
 * @author lipingan (lipingan.dev@outlook.com)
 * @brief RunLoop model law tests: TLS identity, affiliation, lifecycle.
 */
#include "base/run_loop.h"

#include <gtest/gtest.h>

#include "base/task/task_loop.h"

namespace cts::base {
namespace {

TEST(RunLoop, CurrentIsNullptrOffAnyLoop) {
  EXPECT_EQ(RunLoop::Current(), nullptr);
}

TEST(RunLoop, CurrentIsInstalledDuringRun) {
  TaskLoop loop{"test-loop"};
  bool ran = false;
  loop.PostTask([&] {
    ran = true;
    EXPECT_EQ(RunLoop::Current(), &loop);
    EXPECT_TRUE(loop.BelongsToCurrentThread());
    EXPECT_EQ(loop.name(), "test-loop");
    loop.RequestStop();  // self-stop: idempotent, own thread allowed
  });
  loop.Run();
  EXPECT_TRUE(ran);
  // Uninstalled on the way out
  EXPECT_EQ(RunLoop::Current(), nullptr);
}

TEST(RunLoop, WakeupDoesNotStop) {
  TaskLoop loop{"wake-test"};
  int ran = 0;
  loop.PostTask([&] {
    ++ran;
    loop.Wakeup();  // wake word only; the loop must keep running
  });
  loop.PostTask([&] {
    ++ran;
    loop.RequestStop();
  });
  loop.Run();
  EXPECT_EQ(ran, 2);
}

}  // namespace
}  // namespace cts::base
