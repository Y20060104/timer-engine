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
  // 推动到到期 tick
  for (int i = 0; i < 100; ++i)
    wheel.tick();

  EXPECT_EQ(fired, kNumTimers);
}

// 定时器跨三级随机分布 —— 模拟真实业务中 CD 时长不一的混合负载
// delay ∈ [1, 262143]（覆盖 L1 / L2 / L3），总定时器 50000 个。
// 到期后不应漏触发。
TEST(StressTest, RandomDelayAcrossAllLevels)
{
  TimerWheel<HeapAlloc> wheel;
  constexpr int kNumTimers = 50000;
  constexpr int kMaxTick = 262144;
  std::vector<int> expected_fire_at(kMaxTick + 1, 0);
  std::vector<int> actual_fire_at(kMaxTick + 1, 0);

  // 固定种子保证可复现
  std::mt19937 rng(42);
  std::uniform_int_distribution<int> dist(1, 262143);

  for (int i = 0; i < kNumTimers; ++i)
  {
    uint64_t delay = dist(rng);
    expected_fire_at[delay] += 1;
    wheel.add(delay, [&actual_fire_at, delay] { actual_fire_at[delay] += 1; });
  }

  // 走过全部 tick，让所有定时器有机会到期
  for (int t = 1; t <= kMaxTick; ++t)
  {
    wheel.tick();
  }

  // 对比每个 delay 上的触发次数 —— 任一不等则失败
  for (int i = 1; i <= kMaxTick; ++i)
  {
    EXPECT_EQ(actual_fire_at[i], expected_fire_at[i])
        << "delay=" << i << ": expected " << expected_fire_at[i] << " fires, got "
        << actual_fire_at[i];
  }
}

// 海量插入 + 全部取消 —— 模拟活动开始前紧急关停
// 确保 cancel 不会因为定时器数量增加而退化，且回调永不触发。
TEST(StressTest, MassAddThenCancelAll)
{
  TimerWheel<HeapAlloc> wheel;
  constexpr int kNumTimers = 20000;
  std::vector<Timer *> timers;
  int fired = 0;

  for (int i = 0; i < kNumTimers; ++i)
  {
    timers.push_back(wheel.add(50, [&fired] { ++fired; }));
  }
  for (auto *t : timers)
  {
    wheel.cancel(t);
  }
  // 走过足够远的 tick
  for (int i = 0; i < 300; ++i)
    wheel.tick();

  EXPECT_EQ(fired, 0);
}

// cascade 压力：在第一级刚好走满一圈的前后大量插入
// 目的是触发 L2 → L1 降级链条，保证 cascade 路径无遗漏。
TEST(StressTest, CascadeBoundaryStress)
{
  TimerWheel<HeapAlloc> wheel;
  int fired = 0;

  // 把 tick 推到 L1 快绕回的时刻
  for (int i = 0; i < 254; ++i)
    wheel.tick();

  // 插入一批 delay=3（越过 256 边界，落到 L2 进入 cascade 路径）
  for (int i = 0; i < 1000; ++i)
    wheel.add(3, [&fired] { ++fired; });

  // 再插入一批 delay=1（留在 L1，到期点紧贴边界）
  for (int i = 0; i < 1000; ++i)
    wheel.add(1, [&fired] { ++fired; });

  // 推进到全部到期
  for (int i = 0; i < 10; ++i)
    wheel.tick();

  EXPECT_EQ(fired, 2000);
}

// 递归添加：回调中创建新定时器 —— 模拟"血瓶 CD 结束自动续杯"
// 回调里再 add 新定时器时能正确插入，不会死循环或崩溃。
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
    {
      wheel.add(1, cb); // 回调中再次 add 自己
    }
  };

  wheel.add(1, cb);
  for (int i = 0; i < kMaxReentrant + 2; ++i)
    wheel.tick();

  EXPECT_EQ(count, kMaxReentrant);
}

// 长时间运行无泄漏无翻车 —— 模拟 7×24 服务器长期在线
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
    // 每个 step 随机插入 0~2 个定时器
    int to_add = dist(rng) % 3;
    for (int j = 0; j < to_add; ++j)
    {
      wheel.add(dist(rng), [&fired] { ++fired; });
    }
    wheel.tick();
  }
  // 走过最后一个可能的最大 delay
  for (int i = 0; i < 5000; ++i)
    wheel.tick();

  // 不硬断言 fired 的具体值（由随机种子决定），
  // 本用例主要验证无 crash、无 ASan 报错、无泄漏。
  SUCCEED() << "fired=" << fired;
}

// TimerPool 耗尽后 fallback 被移除，验证不会出现空指针解引用
// 故意用很小的池插入超量定时器，期望程序直接 detection-friendly 崩溃。
TEST(StressTest, PoolExhaustionDoesNotSilentCorrupt)
{
  constexpr size_t kTinyPool = 100;
  constexpr int kOvershoot = 200;
  TimerWheel<TimerPool<kTinyPool>> wheel;
  std::vector<Timer *> timers;

  // 池子只能装 100 个，但我们要分配 200 个
  // 当前实现无 fallback —— 第 101 次 allocate 解引用 nullptr。
  // 用 EXPECT_DEATH 验证：发现池子不够时会立即暴露。
  EXPECT_DEATH(
      {
        for (int i = 0; i < kOvershoot; ++i)
        {
          timers.push_back(wheel.add(100, [] {}));
        }
      },
      ".*");
}

// 同一个 tick 内部分被取消、部分正常触发 —— 模拟混合作战
// 检验 cancelled 标记不会误伤相邻节点。
TEST(StressTest, PartialCancelInSameSlot)
{
  TimerWheel<HeapAlloc> wheel;
  constexpr int kSameSlot = 500;
  int fired = 0;

  std::vector<Timer *> timers;
  for (int i = 0; i < kSameSlot; ++i)
  {
    // 所有定时器映射到 L1 的同一个槽（delay 恰好等于 slot idx）
    timers.push_back(wheel.add(10, [&fired] { ++fired; }));
  }
  // 取消奇数索引的定时器
  for (int i = 1; i < kSameSlot; i += 2)
  {
    wheel.cancel(timers[i]);
  }
  for (int i = 0; i < 11; ++i)
    wheel.tick();

  EXPECT_EQ(fired, kSameSlot / 2);
}

// 第三级 cascade 路径：定时器 delay 刚好跨越 L3 边界
// 目的：验证 L3 → L2 → L1 双层 cascade 的路径完整性。
TEST(StressTest, TripleLevelCascade)
{
  TimerWheel<HeapAlloc> wheel;
  int fired = 0;

  // delay 恰好等于 L3 边界: 16384（(1<<14)）
  constexpr int kL3Boundary = 16384;
  constexpr int kN = 500;

  for (int i = 0; i < kN; ++i)
  {
    wheel.add(kL3Boundary, [&fired] { ++fired; });
  }
  for (int i = 0; i < kL3Boundary; ++i)
    wheel.tick();

  EXPECT_EQ(fired, kN);
}
