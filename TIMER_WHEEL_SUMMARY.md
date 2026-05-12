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

template<typename Alloc>
class TimerWheel {
public:
    TimerWheel(Alloc alloc = Alloc{})
        : slots_l1_{}, slots_l2_{}, slots_l3_{}, current_tick_(0)
        , alloc_(std::move(alloc)) {}
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
    Alloc    alloc_;          // 编译期绑定，零调用开销
};
```

### TimerPool（内存池）

```cpp
template<size_t N>
class TimerPool {
public:
    TimerPool();                                       // new Timer[N]，链表串联
    Timer* allocate();                                 // 头部取出，O(1)
    void deallocate(Timer* t);                        // 头部归还，O(1)
    TimerPool(const TimerPool&) = delete;             // 禁止拷贝（Rule of 3）
    TimerPool(TimerPool&&) noexcept;                  // move 语义
    TimerPool& operator=(TimerPool&&) noexcept;
private:
    Timer* pool_;       // 连续内存，cache-friendly
    Timer* free_list;   // 单链表复用 next_ 指针
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
    if((current_tick_ & SLOTS_LEVEL2_MASK) == 0) {
        cascade(3, (current_tick_ >> 14) & SLOTS_LEVEL3_MASK);
    }
    cascade(2, (current_tick_ >> 8) & SLOTS_LEVEL2_MASK);
}
```

---

## 项目目录结构

```
timer-engine/
├── CLAUDE.md               # AI对话行为约束 + 构建指引
├── CMakeLists.txt          # 根目录构建配置（timer_lib 为 INTERFACE）
├── format.sh               # 代码格式化脚本
├── run_tests.sh            # 编译+运行测试脚本
├── run_bench.sh            # Benchmark（Release + O2）
├── TIMER_WHEEL_SUMMARY.md  # 设计文档（即本文件）
├── src/
│   ├── timer_wheel.h       # TimerWheel 模板（header-only）
│   └── timer_pool.h        # TimerPool 模板（header-only）
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
| 分配器集成 | 模板参数 | 编译期绑定，零调用开销，不破坏 TimerWheel 内聚 |
| 内存池 | 预分配+free list | 连续内存 cache-friendly，allocate/deallocate 均 O(1) |
| 拷贝策略 | = delete | 禁拷贝，只保留 move，防止浅拷贝导致双释放 |

---

## Benchmark 实测数据（100万定时器）

| 操作 | TimerWheel + HeapAlloc | TimerWheel + TimerPool | NaiveTimerHeap |
|------|----------------------|------------------------|----------------|
| add (avg/op) | 115ns | **26ns** | 112ns |
| tick (avg/op) | 41ns | **31ns** | 117ns |

**结论**：TimerPool 消除 add 热路径上的 malloc，add 延迟降低约 **4.4x**（115ns → 26ns）。
tick 时 TimerWheel 均 O(1)，远快于堆的 O(log n)，堆排序开销在 tick 路径上差距最大（31ns vs 117ns，~3.8x）。

---

## 未来优化方向

### 已完成 ✅

1. **benchmark** — TimerWheel vs NaiveTimerHeap 对比，add 和 tick 两项数据
2. **析构函数** — 遍历三级所有槽释放所有 Timer
3. **内存池集成** — TimerPool 模板，预分配连续内存 + free list，add 延迟降低 4.4x

### 优先级高（下一步实现）

**4. 线程安全**
- 当前实现是单线程的
- 游戏服务器场景：IO线程add定时器，逻辑线程tick
- 方案一：加mutex（简单，有锁竞争）
- 方案二：lock-free的add（用atomic CAS操作）
- 注意：加了线程安全后需要切换到TSan验证

### 优先级中（对标BQLog日志组件后集成）

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

Q: TimerPool为什么比new/delete快？
A: 预分配连续内存（cache-friendly），free list用侵入式单链表
   复用next_指针，allocate/deallocate均O(1)无系统调用。
   实测add延迟从115ns降到26ns（~4.4x）。

Q: 如何保证线程安全？
A: 当前单线程。多线程方案：
   IO线程add用lock-free队列暂存，
   逻辑线程tick时统一消费队列再insert。
```
