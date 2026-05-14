#include "timer_pool.h"
#include "timer_wheel.h"
#include <gtest/gtest.h>
#include <random>
#include <vector>

struct HeapAlloc
{
  Timer *allocate() { return new Timer; }
  void deallocate(Timer *t) { delete t; }
};

// ============================================================================
// 基础功能测试
// ============================================================================

// 正常运行
TEST(TimerWheel, NormalFire)
{
  TimerWheel<HeapAlloc> wheel;
  bool fired = false;
  wheel.add(5, [&fired] { fired = true; });
  for (int i = 0; i < 5; ++i)
    wheel.tick();
  EXPECT_TRUE(fired);
}

// 回绕正确检验（边界情况）
TEST(TimerWheel, WrapAround)
{
  TimerWheel<HeapAlloc> wheel;
  bool fired = false;
  for (int i = 0; i < 250; ++i)
    wheel.tick();
  wheel.add(10, [&fired] { fired = true; });
  for (int i = 0; i < 10; ++i)
    wheel.tick();
  EXPECT_TRUE(fired);
}

// 取消定时器
TEST(TimerWheel, CancelTimer)
{
  TimerWheel<HeapAlloc> wheel;
  bool fired = false;
  Timer *t = wheel.add(10, [&fired] { fired = true; });
  wheel.cancel(t);
  for (int i = 0; i < 256; ++i)
    wheel.tick();
  EXPECT_FALSE(fired);
}

// 第二级触发
TEST(TimerWheel, SecondFire)
{
  TimerWheel<HeapAlloc> wheel;
  bool fired = false;
  Timer *t = wheel.add(300, [&fired] { fired = true; });
  for (int i = 0; i < 300; ++i)
    wheel.tick();
  EXPECT_TRUE(fired);
}

// 第二级降级触发
TEST(TimerWheel, Second_DeLevel_Fire)
{
  TimerWheel<HeapAlloc> wheel;
  bool fired = false;
  Timer *t = wheel.add(256, [&fired] { fired = true; });
  for (int i = 0; i < 256; ++i)
    wheel.tick();
  EXPECT_TRUE(fired);
}

// 第三级触发
TEST(TimerWheel, ThirdFire)
{
  TimerWheel<HeapAlloc> wheel;
  bool fired = false;
  Timer *t = wheel.add(20000, [&fired] { fired = true; });
  for (int i = 0; i < 20000; ++i)
    wheel.tick();
  EXPECT_TRUE(fired);
}

// current_ticks初始不为0测试
TEST(TimerWheel,ProcessingTick)
{
  TimerWheel<HeapAlloc> wheel;
  bool fired=false;
  for (int i = 0; i < 1000; ++i)
    wheel.tick();
  Timer *t=wheel.add(5,[&fired]{fired=true;});
  for (int i = 0; i < 5; ++i)
    wheel.tick();
  EXPECT_TRUE(fired);
}

// ============================================================================
// 上行突发（压力测试）：模拟服务器上线后可能遇到的极端场景
// ============================================================================

// 大量定时器在同一 tick 到期（技能 CD 对齐爆发）
// 场景：10000 个玩家同时放技能，delay 均为 100 tick，到期那一刻回调必须全部触发。
TEST(StressTest, BurstExpirySameSlot)
{
  TimerWheel<HeapAlloc> wheel;
  constexpr int kNumTimers = 10000;
  int fired = 0;

  for (int i = 0; i < kNumTimers; ++i)
  {
    wheel.add(100, [&fired] { ++fired; });
  }
  for (int i = 0; i < 100; ++i)
    wheel.tick();

  EXPECT_EQ(fired, kNumTimers);
}

// 定时器跨三级随机分布 —— 模拟真实业务中 CD 时长不一的混合负载
// delay in [1, 262143]（覆盖 L1 / L2 / L3），总定时器 50000 个。
TEST(StressTest, RandomDelayAcrossAllLevels)
{
  TimerWheel<HeapAlloc> wheel;
  constexpr int kNumTimers = 50000;
  constexpr int kMaxTick = 262144;
  std::vector<int> expected_fire_at(kMaxTick + 1, 0);
  std::vector<int> actual_fire_at(kMaxTick + 1, 0);

  std::mt19937 rng(42);
  std::uniform_int_distribution<int> dist(1, 262143);

  for (int i = 0; i < kNumTimers; ++i)
  {
    uint64_t delay = dist(rng);
    expected_fire_at[delay] += 1;
    wheel.add(delay, [&actual_fire_at, delay] { actual_fire_at[delay] += 1; });
  }
  for (int t = 1; t <= kMaxTick; ++t)
    wheel.tick();

  for (int i = 1; i <= kMaxTick; ++i)
  {
    EXPECT_EQ(actual_fire_at[i], expected_fire_at[i])
        << "delay=" << i << ": expected " << expected_fire_at[i] << " fires, got "
        << actual_fire_at[i];
  }
}

// 海量插入 + 全部取消 —— 模拟活动开始前紧急关停
TEST(StressTest, MassAddThenCancelAll)
{
  TimerWheel<HeapAlloc> wheel;
  constexpr int kNumTimers = 20000;
  std::vector<Timer *> timers;
  int fired = 0;

  for (int i = 0; i < kNumTimers; ++i)
    timers.push_back(wheel.add(50, [&fired] { ++fired; }));
  for (auto *t : timers)
    wheel.cancel(t);
  for (int i = 0; i < 300; ++i)
    wheel.tick();

  EXPECT_EQ(fired, 0);
}

// cascade 压力：在 L1 刚好走满一圈的前后大量插入
// 目的是触发 L2 → L1 降级链条，保证 cascade 路径无遗漏。
TEST(StressTest, CascadeBoundaryStress)
{
  TimerWheel<HeapAlloc> wheel;
  int fired = 0;

  for (int i = 0; i < 254; ++i)
    wheel.tick();

  // delay=3 越过 256 边界，落到 L2 → cascade 路径
  for (int i = 0; i < 1000; ++i)
    wheel.add(3, [&fired] { ++fired; });
  // delay=1 留在 L1，到期点紧贴边界
  for (int i = 0; i < 1000; ++i)
    wheel.add(1, [&fired] { ++fired; });

  for (int i = 0; i < 10; ++i)
    wheel.tick();

  EXPECT_EQ(fired, 2000);
}

// 递归添加：回调中创建新定时器 —— 模拟"血瓶 CD 结束自动续杯"
TEST(StressTest, ReentrantAddFromCallback)
{
  TimerWheel<HeapAlloc> wheel;
  int count = 0;
  constexpr int kMaxReentrant = 5;

  std::function<void()> cb;
  cb = [&]()
  {
    ++count;
    if (count < kMaxReentrant)
      wheel.add(1, cb);
  };

  wheel.add(1, cb);
  for (int i = 0; i < kMaxReentrant + 2; ++i)
    wheel.tick();

  EXPECT_EQ(count, kMaxReentrant);
}

// 长时间运行无泄漏无翻车 —— 模拟 7x24 服务器长期在线
// 持续 add + tick，走过多轮 cascade 周期。用 ASan 检测内存问题。
TEST(StressTest, LongRunningNoLeak)
{
  TimerWheel<HeapAlloc> wheel;
  constexpr int kSteps = 50000;
  int fired = 0;

  std::mt19937 rng(99);
  std::uniform_int_distribution<int> dist(1, 5000);

  for (int step = 0; step < kSteps; ++step)
  {
    int to_add = dist(rng) % 3;
    for (int j = 0; j < to_add; ++j)
      wheel.add(dist(rng), [&fired] { ++fired; });
    wheel.tick();
  }
  for (int i = 0; i < 5000; ++i)
    wheel.tick();

  SUCCEED() << "fired=" << fired;
}

// TimerPool 耗尽后 fallback 被移除，验证不会静默损坏
TEST(StressTest, PoolExhaustionDoesNotSilentCorrupt)
{
  constexpr size_t kTinyPool = 100;
  constexpr int kOvershoot = 200;
  TimerWheel<TimerPool<kTinyPool>> wheel;

  // 池子只能装 100 个，第 101 次 allocate 解引用 nullptr 崩溃
  EXPECT_DEATH(
      {
        for (int i = 0; i < kOvershoot; ++i)
          wheel.add(100, [] {});
      },
      ".*");
}

// 同一 tick 内部分取消、部分正常触发 —— 检验 cancelled 标记不误伤相邻节点
TEST(StressTest, PartialCancelInSameSlot)
{
  TimerWheel<HeapAlloc> wheel;
  constexpr int kSameSlot = 500;
  int fired = 0;
  std::vector<Timer *> timers;

  for (int i = 0; i < kSameSlot; ++i)
    timers.push_back(wheel.add(10, [&fired] { ++fired; }));
  for (int i = 1; i < kSameSlot; i += 2)
    wheel.cancel(timers[i]);
  for (int i = 0; i < 11; ++i)
    wheel.tick();

  EXPECT_EQ(fired, kSameSlot / 2);
}

// 第三级 cascade 路径：L3→L2→L1 双层降级的完整性
TEST(StressTest, TripleLevelCascade)
{
  TimerWheel<HeapAlloc> wheel;
  int fired = 0;
  constexpr int kL3Boundary = 16384; // (1<<14)
  constexpr int kN = 500;

  for (int i = 0; i < kN; ++i)
    wheel.add(kL3Boundary, [&fired] { ++fired; });
  for (int i = 0; i < kL3Boundary; ++i)
    wheel.tick();

  EXPECT_EQ(fired, kN);
}
