# 三级时间轮项目总结

## 项目概览

基于层级时间轮算法的高性能定时器框架，支持百万级定时器O(1)均摊调度。
对标游戏服务器场景（天美/网易）：技能CD、buff持续时间、心跳超时。

---

## 已完成的实现

### 核心数据结构

```cpp
struct Timer {
    Timer*    next_;
    Timer*    prev_;
    uint64_t  expire_;       // 绝对到期tick
    std::function<void()> cb_;
    bool      cancelled_;
};

class TimerWheel {
public:
    TimerWheel() : slots_l1_{}, slots_l2_{}, slots_l3_{}, current_tick_(0) {}
    Timer* add(uint64_t delay_ticks, std::function<void()> cb);
    void   cancel(Timer* t);
    void   tick();

private:
    static constexpr int SLOTS_LEVEL1      = 256;
    static constexpr int SLOTS_LEVEL2      = 64;
    static constexpr int SLOTS_LEVEL3      = 16;
    static constexpr int SLOTS_LEVEL1_MASK = 255;
    static constexpr int SLOTS_LEVEL2_MASK = 63;
    static constexpr int SLOTS_LEVEL3_MASK = 15;

    Timer*   slots_l1_[SLOTS_LEVEL1];
    Timer*   slots_l2_[SLOTS_LEVEL2];
    Timer*   slots_l3_[SLOTS_LEVEL3];
    uint64_t current_tick_;

    void insert(Timer** slots, int slot, Timer* t);
    void cascade(int level, int slot);
};
```

### 槽位计算规则

```
第一级覆盖：expire < 256
  槽位 = expire & 255

第二级覆盖：256 <= expire < 16384
  槽位 = (expire >> 8) & 63

第三级覆盖：16384 <= expire < 262144
  槽位 = (expire >> 14) & 15
```

### tick执行顺序（关键）

```
1. current_tick_ += 1
2. 先cascade（降级高层定时器到第一级）
3. 再处理第一级当前槽（触发回调+释放内存）

顺序不能颠倒：先处理再cascade会导致恰好在边界的定时器
延迟一整圈才触发。
```

### cascade触发条件

```cpp
if((current_tick_ & SLOTS_LEVEL1_MASK) == 0) {
    cascade(2, (current_tick_ >> 8)  & SLOTS_LEVEL2_MASK);
    if(((current_tick_ >> 8) & SLOTS_LEVEL2_MASK) == 0) {
        cascade(3, (current_tick_ >> 14) & SLOTS_LEVEL3_MASK);
    }
}
```

---

## 项目目录结构

```
timer-engine/
├── CLAUDE.md               # AI对话行为约束
├── CMakeLists.txt          # 根目录构建配置
├── format.sh               # 代码格式化脚本
├── run_tests.sh            # 编译+运行测试脚本
├── src/
│   ├── timer_wheel.h
│   └── timer_wheel.cpp
├── test/
│   ├── CMakeLists.txt
│   └── test_timer.cpp
└── bench/
    ├── CMakeLists.txt
    └── bench_timer.cpp
```

---

## 脚本说明

### run_tests.sh
```bash
#!/bin/bash
cd "$(dirname "$0")/build"
cmake .. -DCMAKE_BUILD_TYPE=Debug > /dev/null 2>&1
make -j$(nproc) 2>&1 | tail -5
echo "========== 运行测试 =========="
./test/test_timer
echo "========== 测试完成 =========="
```

### format.sh
```bash
#!/bin/bash
find src test bench -name "*.cpp" -o -name "*.h" | xargs clang-format -i
echo "格式化完成"
```

### CMakeLists.txt（根目录）
```cmake
cmake_minimum_required(VERSION 3.20)
project(timer_engine)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_compile_options(-Wall -Wextra -Wpedantic)

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    add_compile_options(-fsanitize=address,undefined)
    add_link_options(-fsanitize=address,undefined)
endif()

add_library(timer_lib STATIC src/timer_wheel.cpp)
target_include_directories(timer_lib PUBLIC src)

add_subdirectory(test)
add_subdirectory(bench)
```

### test/CMakeLists.txt
```cmake
include(FetchContent)
FetchContent_Declare(
    googletest
    URL https://github.com/google/googletest/archive/v1.14.0.zip
    DOWNLOAD_EXTRACT_TIMESTAMP true
)
FetchContent_MakeAvailable(googletest)

add_executable(test_timer test_timer.cpp)
target_link_libraries(test_timer PRIVATE timer_lib GTest::gtest_main)
```

### bench/CMakeLists.txt
```cmake
add_executable(bench_timer bench_timer.cpp)
target_link_libraries(bench_timer PRIVATE timer_lib)
```

---

## 已通过的测试用例

```
TimerWheel.NormalFire         — delay=5，正常触发
TimerWheel.WrapAround         — current_tick=250，delay=10，绕回触发
TimerWheel.CancelTimer        — cancel后不触发
TimerWheel.SecondFire         — delay=300，第二级触发
TimerWheel.Second_DeLevel_Fire — delay=256，边界降级触发
TimerWheel.ThirdFire          — delay=20000，第三级触发
```

---

## 已知设计决策及理由

| 决策 | 选择 | 理由 |
|------|------|------|
| 链表类型 | 侵入式链表 | 零额外内存分配，热路径无malloc |
| 取消方式 | cancelled标记 | O(1)，无需知道节点在哪个槽 |
| 时间单位 | tick（非毫秒） | 整数运算，避免浮点和单位换算 |
| 槽数 | 256/64/16 | 内存换时间，长延迟定时器数量少 |
| cascade顺序 | 先cascade后触发 | 防止边界定时器延迟一圈 |

---

## 未来优化方向

### 优先级高（下一步实现）

**1. benchmark实现**
- 对比朴素方案（遍历数组）vs 时间轮的性能差距
- 测试场景：均匀分布100万定时器、集中到期10万定时器
- 工具：`bench/bench_timer.cpp`，用`std::chrono`计时
- 目标数据：每次tick耗时、每秒可处理定时器数量

**2. 析构函数**
- 当前`TimerWheel`销毁时，slots里残留的Timer不会被delete
- 需要在析构函数里遍历三级所有槽，释放所有Timer

**3. 线程安全**
- 当前实现是单线程的
- 游戏服务器场景：IO线程add定时器，逻辑线程tick
- 方案一：加mutex（简单，有锁竞争）
- 方案二：lock-free的add（用atomic CAS操作）
- 注意：加了线程安全后需要切换到TSan验证

### 优先级中（对标BQLog日志组件后集成）

**4. 内存池集成**
- 当前每次add都new，每次到期都delete，内存碎片严重
- 方案：为Timer对象实现对象池，复用已释放的Timer内存
- 预期收益：消除热路径上的malloc/free开销

**5. 时间戳精度**
- 当前tick由外部驱动，精度取决于调用方
- 游戏服务器标准：1tick=1ms，逻辑帧15帧/秒
- 可以加入`set_tick_interval(uint64_t ms)`接口

**6. 与日志组件集成**
- 在tick触发时记录：触发时间（TSC）、到期tick、实际触发tick的差值
- 用于统计定时器精度抖动
- 这是日志组件的真实使用场景，驱动日志组件的设计

### 优先级低（简历加分项）

**7. C++20改造**
- `std::function`有虚函数调用开销，替换为模板参数或`std::move_only_function`
- 使用`[[nodiscard]]`修饰`add`返回值，防止调用方忽略Timer*
- 使用concepts约束回调类型

**8. SIMD批量处理**
- 当一个槽里有大量定时器同时到期时，遍历链表是串行的
- 理论上可以用SIMD批量检查cancelled标志
- 实际收益需要benchmark验证

**9. 与游戏房间服务器集成**
- 定时器框架作为房间服务器的基础设施
- 心跳超时：`wheel.add(30000, [room]{ room->on_heartbeat_timeout(); })`
- 技能CD：`wheel.add(cd_ticks, [skill]{ skill->set_ready(); })`

---

## 面试话题准备

```
Q: 为什么用时间轮而不是最小堆？
A: 最小堆插入O(logN)，时间轮O(1)均摊。
   百万定时器场景下差距显著，用benchmark数据说话。

Q: 为什么槽数选256/64/16？
A: 覆盖范围约26万tick（游戏场景够用），
   总内存占用(256+64+16)*8=2688字节，极小。
   槽数是2的幂方便位运算替代取模。

Q: cascade会不会造成性能峰值？
A: 会。第一级走完一圈时，第二级当前槽的所有定时器
   同时降级，是O(N)操作。
   缓解方案：限制单个槽的定时器数量上限。

Q: 如何保证线程安全？
A: 当前单线程。多线程方案：
   IO线程add用lock-free队列暂存，
   逻辑线程tick时统一消费队列再insert。
```
