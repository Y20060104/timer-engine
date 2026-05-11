#!/bin/bash

cd "$(dirname "$0")/build"

# Debug构建（带ASan）
cmake .. -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_FLAGS="" > /dev/null 2>&1
make -j$(nproc) 2>&1 | tail -5

echo "========== 运行测试 =========="
./test/test_timer

echo "========== 测试完成 =========="