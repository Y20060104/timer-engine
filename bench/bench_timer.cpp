#include "timer_pool.h"
#include "timer_wheel.h"
#include <chrono>
#include <fstream>
#include <sstream>
#include <string>

#include <functional>
#include <iostream>
#include <queue>
#include <random>
#include <vector>

struct HeapAlloc
{
  Timer *allocate() { return new Timer; }
  void deallocate(Timer *t) { delete t; } // delete t
};

// ========== 朴素方案：最小堆 ==========

struct HeapTimer
{
  uint64_t expire_;
  std::function<void()> cb_;
  bool operator>(const HeapTimer &o) const { return expire_ > o.expire_; }
};

class NaiveTimerHeap
{
public:
  void add(uint64_t delay_ticks, std::function<void()> cb)
  {
    pq_.push({current_tick_ + delay_ticks, cb});
  }

  void tick()
  {
    ++current_tick_;
    while (!pq_.empty() && pq_.top().expire_ <= current_tick_)
    {
      pq_.top().cb_();
      pq_.pop();
    }
  }

  uint64_t current_tick_ = 0;

private:
  std::priority_queue<HeapTimer, std::vector<HeapTimer>, std::greater<HeapTimer>> pq_;
};

int getRandomNumber()
{
  thread_local std::random_device rd;
  thread_local std::mt19937 gen(rd());

  std::uniform_int_distribution<int> dist(1, 262143);

  return dist(gen);
}

void bench_add(TimerWheel<HeapAlloc> &wheel, NaiveTimerHeap &th,
               TimerWheel<TimerPool<1100000>> &pool_wheel)
{
  uint64_t fired_count = 0;
  auto tw_cb = [&fired_count] { ++fired_count; };
  auto start = std::chrono::steady_clock::now();
  for (int i = 0; i < 1000000; ++i)
  {
    wheel.add(getRandomNumber(), tw_cb);
  }
  auto end = std::chrono::steady_clock::now();
  auto elapsed_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
  std::cout << "[TimerWheel HeapAlloc] add 1000000 timers: " << elapsed_ns
            << " ns , avg: " << elapsed_ns / 1000000 << "ns\n";

  uint64_t fired_count_2 = 0;
  auto cb = [&fired_count_2] { ++fired_count_2; };
  auto start_2 = std::chrono::steady_clock::now();
  for (int i = 0; i < 1000000; ++i)
  {
    th.add(getRandomNumber(), cb);
  }
  auto end_2 = std::chrono::steady_clock::now();
  auto elapsed_ns_2 = std::chrono::duration_cast<std::chrono::nanoseconds>(end_2 - start_2).count();
  std::cout << "[NaiveHeap           ] add 1000000 timers: " << elapsed_ns_2
            << " ns , avg: " << elapsed_ns_2 / 1000000 << "ns\n";

  uint64_t fired_count_3 = 0;
  auto ptw_cb = [&fired_count_3] { ++fired_count_3; };
  auto start_3 = std::chrono::steady_clock::now();
  for (int i = 0; i < 1000000; ++i)
  {
    pool_wheel.add(getRandomNumber(), ptw_cb);
  }
  auto end_3 = std::chrono::steady_clock::now();
  auto elapsed_ns_3 = std::chrono::duration_cast<std::chrono::nanoseconds>(end_3 - start_3).count();
  std::cout << "[TimerWheel TimerPool] add 1000000 timers: " << elapsed_ns_3
            << " ns , avg: " << elapsed_ns_3 / 1000000 << "ns\n";
}

void bench_tick(TimerWheel<HeapAlloc> &wheel, NaiveTimerHeap &th,
                TimerWheel<TimerPool<1100000>> &pool_wheel)
{
  uint64_t fired_count = 0;
  auto tw_cb = [&fired_count] { ++fired_count; };

  for (int i = 0; i < 1000000; ++i)
  {
    wheel.add(17000, tw_cb);
  }
  auto start = std::chrono::steady_clock::now();
  for (int i = 0; i < 17000; ++i)
  {
    wheel.tick();
  }
  auto end = std::chrono::steady_clock::now();
  auto elapsed_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
  std::cout << "[TimerWheel HeapAlloc] tick 1000000 timers: " << elapsed_ns
            << " ns , avg: " << elapsed_ns / 1000000 << "ns\n";
  uint64_t fired_count_2 = 0;
  auto cb = [&fired_count_2] { ++fired_count_2; };

  for (int i = 0; i < 1000000; ++i)
  {
    th.add(17000, cb);
  }
  auto start_2 = std::chrono::steady_clock::now();
  for (int i = 0; i < 17000; ++i)
  {
    th.tick();
  }
  auto end_2 = std::chrono::steady_clock::now();
  auto elapsed_ns_2 = std::chrono::duration_cast<std::chrono::nanoseconds>(end_2 - start_2).count();
  std::cout << "[NaiveHeap           ] tick 1000000 timers: " << elapsed_ns_2
            << " ns , avg: " << elapsed_ns_2 / 1000000 << "ns\n";

  uint64_t fired_count_3 = 0;
  auto ptw_cb = [&fired_count_3] { ++fired_count_3; };

  for (int i = 0; i < 1000000; ++i)
  {
    pool_wheel.add(17000, ptw_cb);
  }
  auto start_3 = std::chrono::steady_clock::now();
  for (int i = 0; i < 17000; ++i)
  {
    pool_wheel.tick();
  }
  auto end_3 = std::chrono::steady_clock::now();
  auto elapsed_ns_3 = std::chrono::duration_cast<std::chrono::nanoseconds>(end_3 - start_3).count();
  std::cout << "[TimerWheel TimerPool] tick 1000000 timers: " << elapsed_ns_3
            << " ns , avg: " << elapsed_ns_3 / 1000000 << "ns\n";
}

// 读取进程常驻内存 (VmRSS)，单位 MB
static long GetVmRSSMB()
{
  std::ifstream f("/proc/self/status");
  std::string line;
  while (std::getline(f, line))
  {
    if (line.rfind("VmRSS:", 0) != 0)
      continue;
    // 行格式: "VmRSS:    12345 kB"，用 istringstream 跳过标签
    std::istringstream iss(line);
    std::string label;
    long kb;
    iss >> label >> kb;
    return kb / 1024;
  }
  return -1;
}

// 百万定时器内存开销对比
// HeapAlloc（按需 new/delete）vs TimerPool（预分配连续数组+union free list）
void bench_mem()
{
  constexpr int kNumTimers = 1000000;
  constexpr int kPoolSize = 1100000;
  constexpr int kDelay = 17000;

  std::cout << "\n========== 百万定时器内存开销对比 ==========\n";

  // 注意：顺序敏感 —— 先跑内存更紧凑的一方，避免被对侧 malloc arena 残留污染。
  // 每个 scope 结束后 wheel 析构，malloc 通常不归还页给 OS，所以 VmRSS 只升不降。

  // ---------- TimerPool：预分配连续数组 ----------
  long mem_before = GetVmRSSMB();
  {
    TimerWheel<TimerPool<kPoolSize>> wheel;
    int fired = 0;
    for (int i = 0; i < kNumTimers; ++i)
      wheel.add(kDelay, [&fired] { ++fired; });
    long mem_after_add = GetVmRSSMB();

    for (int i = 0; i < kDelay; ++i)
      wheel.tick();
    std::cout << "[TimerPool 1M] mem: +" << (mem_after_add - mem_before) << " MB"
              << " (pool=" << kPoolSize << ")\n";
  }

  // ---------- HeapAlloc：按需 new/delete ----------
  mem_before = GetVmRSSMB();
  {
    TimerWheel<HeapAlloc> wheel;
    int fired = 0;
    for (int i = 0; i < kNumTimers; ++i)
      wheel.add(kDelay, [&fired] { ++fired; });
    long mem_after_add = GetVmRSSMB();

    for (int i = 0; i < kDelay; ++i)
      wheel.tick();
    std::cout << "[HeapAlloc  1M] mem: +" << (mem_after_add - mem_before) << " MB\n";
  }
}

int main()
{
  // 内存对比必须最先跑，避免前面的 bench_add/bench_tick 撑大
  // glibc malloc arena 后再跑读数失真。
  bench_mem();

  TimerWheel<HeapAlloc> wheel;
  NaiveTimerHeap th;
  TimerWheel<TimerPool<1100000>> pool_wheel;
  std::cout << "\n插入100万定时器 [TimerWheel ] 对比 [NaiveHeap  ]...\n";
  bench_add(wheel, th, pool_wheel);
  std::cout << "100万定时器同时到期 [TimerWheel ] 对比 [NaiveHeap  ]...\n";
  TimerWheel<HeapAlloc> wheel_2;
  NaiveTimerHeap th_2;
  TimerWheel<TimerPool<1100000>> pool_wheel_2;
  bench_tick(wheel_2, th_2, pool_wheel_2);
}