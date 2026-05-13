#pragma once
#include <cstdint>
#include <functional>

struct Timer
{
  Timer *next_;
  Timer *prev_;
  uint64_t expire_;
  std::function<void()> cb_;
  bool cancelled_;
};

template <typename Alloc> class TimerWheel
{
public:
  Timer *add(uint64_t delay_ticks, std::function<void()> cb);
  void cancel(Timer *t);
  void tick();
  void insert(Timer **slots, int slot, Timer *t);
  void cascade(int level, int slot);

public:
  TimerWheel(Alloc alloc = Alloc{})
      : slots_l1{}, slots_l2{}, slots_l3{}, current_tick_(0), alloc_(std::move(alloc))
  {
  }
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
  Alloc alloc_;
};

template <typename Alloc>
Timer *TimerWheel<Alloc>::add(uint64_t delay_ticks, std::function<void()> cb)
{
  uint64_t expected_time = current_tick_ + delay_ticks;
  Timer *new_timer = alloc_.allocate();
  *new_timer = {nullptr, nullptr, expected_time, cb, false};

  if (expected_time < 256)
  {
    // 插入第一级
    insert(slots_l1, expected_time & SLOTS_LEVEL1_MASK, new_timer);
  }
  else if (expected_time < 256 * 64)
  {
    insert(slots_l2, (expected_time >> 8) & SLOTS_LEVEL2_MASK, new_timer);
  }
  else if (expected_time >= 256 * 64)
  {
    insert(slots_l3, (expected_time >> 14) & SLOTS_LEVEL3_MASK, new_timer);
  }
  return new_timer;
}

template <typename Alloc> void TimerWheel<Alloc>::insert(Timer **slots, int slot, Timer *t)
{
  t->next_ = slots[slot];
  t->prev_ = nullptr;
  if (slots[slot] != nullptr)
  {
    slots[slot]->prev_ = t;
  }
  slots[slot] = t;
}

template <typename Alloc> void TimerWheel<Alloc>::cancel(Timer *t) { t->cancelled_ = true; }

template <typename Alloc> void TimerWheel<Alloc>::tick()
{
  current_tick_ += 1;

  if ((current_tick_ & SLOTS_LEVEL1_MASK) == 0)
  {
    if (((current_tick_)&SLOTS_LEVEL2_MASK) == 0)
    {
      cascade(3, (current_tick_ >> 14) & SLOTS_LEVEL3_MASK);
    }
    cascade(2, (current_tick_ >> 8) & SLOTS_LEVEL2_MASK);
  }

  Timer *head = slots_l1[(current_tick_)&SLOTS_LEVEL1_MASK];
  while (head)
  {
    if (!head->cancelled_)
    {
      head->cb_(); // cancelled就跳过回调
    }
    Timer *next = head->next_;
    alloc_.deallocate(head);
    head = next;
  }
  slots_l1[(current_tick_)&SLOTS_LEVEL1_MASK] = nullptr;
}

template <typename Alloc> void TimerWheel<Alloc>::cascade(int level, int slot)
{
  switch (level)
  {
  case 2:
    // 取出整个链表
    {
      Timer *head = slots_l2[slot];
      while (head)
      {
        Timer *next = head->next_;
        insert(slots_l1, (head->expire_) & SLOTS_LEVEL1_MASK, head);
        head = next;
      }
      slots_l2[slot] = nullptr;
      break;
    }
  case 3:
  {
    Timer *head = slots_l3[slot];
    while (head)
    {
      Timer *next = head->next_;
      insert(slots_l2, (head->expire_ >> 8) & SLOTS_LEVEL2_MASK, head);
      head = next;
    }
    slots_l3[slot] = nullptr;
    break;
  }
  default:
    break;
  }
}

template <typename Alloc> TimerWheel<Alloc>::~TimerWheel()
{
  for (int i = 0; i < SLOTS_LEVEL1; ++i)
  {
    Timer *head = slots_l1[i];
    while (head)
    {
      Timer *next = head->next_;
      alloc_.deallocate(head);
      head = next;
    }
  }

  for (int i = 0; i < SLOTS_LEVEL2; ++i)
  {
    Timer *head = slots_l2[i];
    while (head)
    {
      Timer *next = head->next_;
      alloc_.deallocate(head);
      head = next;
    }
  }

  for (int i = 0; i < SLOTS_LEVEL3; ++i)
  {
    Timer *head = slots_l3[i];
    while (head)
    {
      Timer *next = head->next_;
      alloc_.deallocate(head);
      head = next;
    }
  }
}
