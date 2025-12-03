#!/usr/bin/env bash
# 兼容性：如果当前 shell 不支持 pipefail，就降级为不使用 pipefail
if (set -o pipefail) 2>/dev/null; then
  set -euo pipefail
else
  set -eu
fi

# 用法: ./build.sh [TARGET] [CONFIG]
BUILD_DIR=cmake-build
TARGET=${1:-}      # 如果不传则为空，表示构建默认目标
CONFIG=${2:-Debug}

mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

cmake .. -DCMAKE_BUILD_TYPE="$CONFIG"

if [ -n "$TARGET" ]; then
  cmake --build . --config "$CONFIG" --target "$TARGET"
else
  cmake --build . --config "$CONFIG"
fi