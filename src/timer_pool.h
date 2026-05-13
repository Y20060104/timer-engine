#pragma once
#include "timer_wheel.h"

template <size_t N> class TimerPool {
public:
  TimerPool();
  TimerPool(const TimerPool &) = delete;
  TimerPool &operator=(const TimerPool &) = delete;
  TimerPool(TimerPool &&other) noexcept;
  TimerPool &operator=(TimerPool &&other) noexcept;
  ~TimerPool();
  Timer *allocate();
  void deallocate(Timer *t);

private:
  union TimerSlot {
    TimerSlot *next_free;
    Timer timer;

    TimerSlot() : next_free(nullptr) {}
    ~TimerSlot() {}
  };

  TimerSlot *pool_;
  TimerSlot *free_list_;
};

template <size_t N> TimerPool<N>::TimerPool() {
  pool_ = new TimerSlot[N];
  for (int i = 0; i < N - 1; i++) {
    pool_[i].next_free = &pool_[i + 1];
  }
  pool_[N - 1].next_free = nullptr;
  free_list_ = &pool_[0];
}

template <size_t N> TimerPool<N>::~TimerPool() { delete[] pool_; }

template <size_t N> Timer *TimerPool<N>::allocate() {
  TimerSlot *slot = free_list_;
  free_list_ = slot->next_free;
  return new (&slot->timer) Timer;
}

template <size_t N> void TimerPool<N>::deallocate(Timer *t) {
  t->~Timer();
  TimerSlot *slot = reinterpret_cast<TimerSlot *>(t);
  slot->next_free = free_list_;
  free_list_ = slot;
}

template <size_t N>
TimerPool<N>::TimerPool(TimerPool &&other) noexcept
    : pool_(other.pool_), free_list_(other.free_list_) {
  other.pool_ = nullptr;
  other.free_list_ = nullptr;
}

template <size_t N>
TimerPool<N> &TimerPool<N>::operator=(TimerPool &&other) noexcept {
  if (this == &other)
    return *this;
  delete[] pool_;
  pool_ = other.pool_;
  free_list_ = other.free_list_;
  other.pool_ = nullptr;
  other.free_list_ = nullptr;
  return *this;
}
