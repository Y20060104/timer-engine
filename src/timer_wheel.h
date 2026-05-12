#pragma once
#include <cstdint>
#include <functional>

struct Timer {
  Timer *next_;
  Timer *prev_;
  uint64_t expire_;
  std::function<void()> cb_;
  bool cancelled_;
};

class TimerWheel {
public:
 Timer* add(uint64_t delay_ticks, std::function<void()> cb);
  void cancel(Timer *t);
  void tick();
  void insert(Timer** slots,int slot,Timer* t);
  void cascade(int level,int slot);

public:
  TimerWheel() : slots_l1{}, slots_l2{}, slots_l3{}, current_tick_(0) {}
  ~TimerWheel();

private:
  static constexpr int SLOTS_LEVEL1 = 256;
  static constexpr int SLOTS_LEVEL2 = 64;
  static constexpr int SLOTS_LEVEL3 = 16;

  static constexpr int SLOTS_LEVEL1_MASK = 255;
  static constexpr int SLOTS_LEVEL2_MASK = 63;
  static constexpr int SLOTS_LEVEL3_MASK = 15;

  Timer *slots_l1[SLOTS_LEVEL1];
  Timer *slots_l2[SLOTS_LEVEL2];
  Timer *slots_l3[SLOTS_LEVEL3];

  uint64_t current_tick_;
};