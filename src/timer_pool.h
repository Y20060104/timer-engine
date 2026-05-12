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
  Timer *pool_;
  Timer *free_list;
};

template <size_t N> TimerPool<N>::TimerPool() {
  pool_ = new Timer[N];
  for (int i = 0; i < N - 1; i++) {
    pool_[i].next_ = &pool_[i + 1];
  }
  pool_[N - 1].next_ = nullptr;
  free_list = &pool_[0];
}

template <size_t N> TimerPool<N>::~TimerPool() { delete[] pool_; }

template <size_t N> Timer *TimerPool<N>::allocate() {
  if (free_list == nullptr) {
    return new Timer; // fallback
  }
  Timer *timer = free_list;
  free_list = free_list->next_;
  return timer;
}

template <size_t N> void TimerPool<N>::deallocate(Timer *t) {
  t->prev_ = nullptr;
  t->next_ = free_list;
  free_list = t;
}

template <size_t N>
TimerPool<N>::TimerPool(TimerPool &&other) noexcept
    : pool_(other.pool_), free_list(other.free_list) {
  other.pool_ = nullptr;
  other.free_list = nullptr;
}

template <size_t N>
TimerPool<N> &TimerPool<N>::operator=(TimerPool &&other) noexcept {
  if (this == &other)
    return *this;
  delete[] pool_;
  this->pool_ = other.pool_;
  this->free_list = other.free_list;
  other.pool_ = nullptr;
  other.free_list = nullptr;
  return *this;
}
