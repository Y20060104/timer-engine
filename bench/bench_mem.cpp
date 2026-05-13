#include "timer_pool.h"
#include "timer_wheel.h"
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>

struct HeapAlloc
{
  Timer *allocate() { return new Timer; }
  void deallocate(Timer *t) { delete t; }
};

static long GetVmRSSMB()
{
  std::ifstream f("/proc/self/status");
  std::string line;
  while (std::getline(f, line))
  {
    if (line.rfind("VmRSS:", 0) != 0)
      continue;
    std::istringstream iss(line);
    std::string label;
    long kb;
    iss >> label >> kb;
    return kb / 1024;
  }
  return -1;
}

template <typename Alloc>
void bench_mem(const char *label, int pool_size = 0)
{
  constexpr int kNumTimers = 1000000;
  constexpr int kDelay = 17000;

  long mem_before = GetVmRSSMB();
  {
    TimerWheel<Alloc> wheel;
    int fired = 0;
    for (int i = 0; i < kNumTimers; ++i)
      wheel.add(kDelay, [&fired] { ++fired; });
    long mem_after_add = GetVmRSSMB();

    for (int i = 0; i < kDelay; ++i)
      wheel.tick();

    std::cout << "[" << label << " 1M] mem: +" << (mem_after_add - mem_before) << " MB";
    if (pool_size > 0)
      std::cout << " (pool=" << pool_size << ")";
    std::cout << "\n";
  }
}

int main(int argc, char **argv)
{
  if (argc != 2)
  {
    std::cerr << "Usage: bench_mem [pool|heap]\n";
    return 1;
  }

  if (std::strcmp(argv[1], "pool") == 0)
    bench_mem<TimerPool<1100000>>("TimerPool", 1100000);
  else if (std::strcmp(argv[1], "heap") == 0)
    bench_mem<HeapAlloc>("HeapAlloc");
  else
  {
    std::cerr << "Usage: bench_mem [pool|heap]\n";
    return 1;
  }
  return 0;
}
