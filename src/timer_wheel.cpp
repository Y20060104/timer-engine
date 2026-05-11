#include "timer_wheel.h"

Timer* TimerWheel::add(uint64_t delay_ticks,std::function<void()>cb)
{
    uint64_t expected_time = current_tick_+delay_ticks;
    
    Timer* new_timer=new Timer;
    new_timer->expire_=expected_time;
    new_timer->cancelled_=false;
    new_timer->cb_=cb;
    
    
    new_timer->next_=slots_[(expected_time)&(SLOTS_MASK)];
    new_timer->prev_=nullptr;
    if(slots_[(expected_time)&(SLOTS_MASK)]!=nullptr){
        slots_[(expected_time)&(SLOTS_MASK)]->prev_=new_timer;
    }
    slots_[(expected_time)&(SLOTS_MASK)]=new_timer;
    return new_timer;

}

void TimerWheel::cancel(Timer* t)
{
    t->cancelled_=true;
}

void TimerWheel::tick()
{
    current_tick_+=1;
    Timer* head=slots_[current_tick_&SLOTS_MASK];
    while(head){
        if(!head->cancelled_){
            head->cb_();// 回调函数
        }
       auto* tmp_timer=head->next_;
        delete head;
        head=tmp_timer;
    }
    slots_[(current_tick_&SLOTS_MASK)]=nullptr;
}