#include "timer_wheel.h"
#include <gtest/gtest.h>

// 正常运行
TEST(TimerWheel, NormalFire) {
  TimerWheel wheel;
  bool fired = false;
  wheel.add(5, [&fired] { fired = true; });
  for (int i = 0; i < 5; ++i)
    wheel.tick();
  EXPECT_TRUE(fired);
}

// 回绕正确检验（边界情况）
TEST(TimerWheel, WrapAround) {
  TimerWheel wheel;
  bool fired = false;
  for (int i = 0; i < 250; ++i)
    wheel.tick();
  wheel.add(10, [&fired] { fired = true; });
  for (int i = 0; i < 10; ++i)
    wheel.tick();
  EXPECT_TRUE(fired);
}

// 取消定时器
TEST(TimerWheel, CancelTimer) {
  TimerWheel wheel;
  bool fired = false;
  Timer *t = wheel.add(10, [&fired] { fired = true; });
  wheel.cancel(t);
  for (int i = 0; i < 256; ++i)
    wheel.tick();
  EXPECT_FALSE(fired);
}

// 第二级触发
TEST(TimerWheel, SecondFire) {
  TimerWheel wheel;
  bool fired = false;
  Timer *t = wheel.add(300, [&fired] { fired = true; });
  for (int i = 0; i < 300; ++i)
    wheel.tick();
  EXPECT_TRUE(fired);
}

// 第二级降级触发
TEST(TimerWheel, Second_DeLevel_Fire) {
  TimerWheel wheel;
  bool fired = false;
  Timer *t = wheel.add(256, [&fired] { fired = true; });
  for (int i = 0; i < 256; ++i)
    wheel.tick();
  EXPECT_TRUE(fired);
}

// 第三级触发
TEST(TimerWheel, ThirdFire) {
  TimerWheel wheel;
  bool fired = false;
  Timer *t = wheel.add(20000, [&fired] { fired = true; });
  for (int i = 0; i < 20000; ++i)
    wheel.tick();
  EXPECT_TRUE(fired);
}