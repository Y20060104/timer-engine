#include <gtest/gtest.h>
#include "timer_wheel.h"

TEST(TimerWheel, NormalFire) {
    TimerWheel wheel;
    bool fired = false;
    wheel.add(5, [&fired]{ fired = true; });
    for(int i = 0; i < 5; ++i) wheel.tick();
    EXPECT_TRUE(fired);
}

TEST(TimerWheel, WrapAround) {
    TimerWheel wheel;
    bool fired = false;
    for(int i = 0; i < 250; ++i) wheel.tick();
    wheel.add(10, [&fired]{ fired = true; });
    for(int i = 0; i < 10; ++i) wheel.tick();
    EXPECT_TRUE(fired);
}

TEST(TimerWheel, CancelTimer) {
    TimerWheel wheel;
    bool fired = false;
    Timer* t = wheel.add(10, [&fired]{ fired = true; });
    wheel.cancel(t);
    for(int i = 0; i < 256; ++i) wheel.tick();
    EXPECT_FALSE(fired);
}