#!/bin/bash
find src test bench -name "*.cpp" -o -name "*.h" | xargs clang-format -i
echo "格式化完成"