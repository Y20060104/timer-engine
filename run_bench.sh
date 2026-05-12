cd "$(dirname "$0")/build"
cmake .. -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_FLAGS="-O2" > /dev/null 2>&1
make -j$(nproc) 2>&1 | tail -3
echo "========== Benchmark =========="
./bench/bench_timer
echo "========== 完成 =========="