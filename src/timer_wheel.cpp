#include "timer_wheel.h"

Timer* TimerWheel::add(uint64_t delay_ticks, std::function<void()> cb) {
  uint64_t expected_time = current_tick_ + delay_ticks;
  Timer *new_timer =new Timer{nullptr, nullptr, expected_time, cb, false};

  if(expected_time<256){
    // 插入第一级
     insert(slots_l1, expected_time & SLOTS_LEVEL1_MASK, new_timer);
  }else if(expected_time<256*64){
     insert(slots_l2, (expected_time >> 8) & SLOTS_LEVEL2_MASK, new_timer);
  }else if(expected_time>=256*64){
   insert(slots_l3, (expected_time >> 14) & SLOTS_LEVEL3_MASK, new_timer);
  }
return new_timer;
}


void TimerWheel::insert(Timer** slots,int slot,Timer* t){
   t->next_=slots[slot];
   t->prev_=nullptr;
    if(slots[slot]!=nullptr){
      slots[slot]->prev_=t;
    }
    slots[slot]=t;
}
void TimerWheel::cancel(Timer *t) { t->cancelled_ = true; }

void TimerWheel::tick() {
  current_tick_ += 1;
 
  if((current_tick_&SLOTS_LEVEL1_MASK)==0){
    if(((current_tick_)&SLOTS_LEVEL2_MASK)==0){
      cascade(3,(current_tick_>>14)&SLOTS_LEVEL3_MASK);
    }
    cascade(2,(current_tick_>>8)&SLOTS_LEVEL2_MASK);
  }

   Timer* head=slots_l1[(current_tick_)&SLOTS_LEVEL1_MASK];
  while(head){
    if(!head->cancelled_) head->cb_();  // cancelled就跳过回调
    Timer* next=head->next_;
    delete head;
    head=next;
  }
  slots_l1[(current_tick_)&SLOTS_LEVEL1_MASK]=nullptr;
}

void TimerWheel::cascade(int level,int slot)
{
  switch (level)
  {
  case 2:
  // 取出整个链表
  {
    Timer* head=slots_l2[slot];
    while(head){
      Timer* next=head->next_;
    insert(slots_l1,(head->expire_)&SLOTS_LEVEL1_MASK,head);
      head=next;
  }
  slots_l2[slot]=nullptr;
    break;
}
  case 3:
  {
 Timer* head=slots_l3[slot];
    while(head){
      Timer* next=head->next_;
    insert(slots_l2,(head->expire_>>8)&SLOTS_LEVEL2_MASK,head);
      head=next;
  }
  slots_l3[slot]=nullptr;
    break;
}
  default:
    break;
  }
}

TimerWheel::~TimerWheel()
{
  for(int i=0;i<SLOTS_LEVEL1;++i){
    Timer* head=slots_l1[i];
    while(head){
      Timer* next=head->next_;
      delete head;
      head=next;
    }
  }
 

  for(int i=0;i<SLOTS_LEVEL2;++i){
    Timer* head=slots_l2[i];
    while(head){
      Timer* next=head->next_;
      delete head;
      head=next;
    }
  }
  
  for(int i=0;i<SLOTS_LEVEL3;++i){
    Timer* head=slots_l3[i];
    while(head){
      Timer* next=head->next_;
      delete head;
      head=next;
    }
  }
  
}
