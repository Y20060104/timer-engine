#pragma once
#include <cstdint>
#include <functional>

struct Timer{
    Timer* next_;
    Timer* prev_;
    uint64_t expire_;
    std::function<void()> cb_;
    bool cancelled_;
};

constexpr int SLOTS_MASK=255;

class TimerWheel{
    public:
    Timer* add(uint64_t delay_ticks , std::function<void()> cb);
    void cancel(Timer* t);
    void tick();

    public:
    TimerWheel() : slots_{}, current_tick_(0) {}
    private:
    Timer* slots_[256];

    uint64_t current_tick_;
};