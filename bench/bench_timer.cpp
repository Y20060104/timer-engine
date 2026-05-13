#include "timer_pool.h"
#include "timer_wheel.h"
#include <chrono>
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

int main()
{
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