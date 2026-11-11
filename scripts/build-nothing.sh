#!/usr/bin/env bash
# Build from the repository root with AOSP clang r450784d.
set -euo pipefail

ROOT=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
TOOLCHAIN_ROOT=${TOOLCHAIN_ROOT:-"$(dirname -- "$ROOT")/nothing-toolchain"}
CLANG_PATH=$TOOLCHAIN_ROOT/clang
export PATH="$TOOLCHAIN_ROOT/host/bin:$CLANG_PATH/bin:$PATH"
export ARCH=arm64 SUBARCH=arm64 LLVM=1 LLVM_IAS=1
export CC="$CLANG_PATH/bin/clang" LD="$CLANG_PATH/bin/ld.lld"
export AR="$CLANG_PATH/bin/llvm-ar" NM="$CLANG_PATH/bin/llvm-nm"
export OBJCOPY="$CLANG_PATH/bin/llvm-objcopy" STRIP="$CLANG_PATH/bin/llvm-strip"
export CROSS_COMPILE=aarch64-linux-gnu- CROSS_COMPILE_COMPAT=arm-linux-gnueabi-
export KBUILD_BUILD_USER=Nothing KBUILD_BUILD_HOST=NothingKernel
export KBUILD_BUILD_VERSION=1
export KBUILD_COMPILER_STRING
KBUILD_COMPILER_STRING=$("$CC" --version | head -n1)

exec make -C "$ROOT" -j"${JOBS:-16}" O="${OUT:-out}" \
    ARCH=arm64 HOSTCC=gcc HOSTCXX=g++ "$@"
