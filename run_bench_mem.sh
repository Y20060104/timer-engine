#!/bin/bash
set -e
cd "$(dirname "$0")/build"
cmake .. -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_FLAGS="-O2" > /dev/null 2>&1
make -j$(nproc) 2>&1 | tail -3
echo ""
echo "========== 百万定时器内存开销对比 (独立进程) =========="
echo ""
./bench/bench_mem pool
./bench/bench_mem heap
echo ""
echo "========== 完成 =========="
