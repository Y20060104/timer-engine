add_executable(bench_timer bench_timer.cpp)
target_link_libraries(bench_timer PRIVATE timer_lib)

add_executable(bench_mem bench_mem.cpp)
target_link_libraries(bench_mem PRIVATE timer_lib)