#!/bin/bash
# Build QuickJS (third_party/quickjs) + jsrun for BB10. From bb10-tools/: native-tube/js/build.sh -> native-tube/build/jsrun
set -o pipefail
cd "$(dirname "$0")/../.."
DOCKER_CONTEXT=colima-aosp docker run --rm -v a11-src:/src -v $PWD/phase6/qnxsdk:/qnxsdk:ro -v $PWD/phase6:/w -v $PWD/native-tube:/t -w /t a11-build bash -c '
. /w/toolchain/qnx-env.sh
set -e
mkdir -p build/qjs
Q=third_party/quickjs
for f in dtoa libregexp libunicode quickjs; do
  $QNX_CC -std=gnu11 -O2 -D_GNU_SOURCE -funsigned-char -fno-strict-aliasing -w -I$Q -c $Q/$f.c -o build/qjs/$f.o &
done
wait
$QNX_CC -std=gnu11 -O2 -Wall -I$Q -c js/jsrun.c -o build/qjs/jsrun.o
$QNX_LD $QNX_LDFLAGS_EXE build/qjs/*.o -lm $QNX_LIBS_EXE -o build/jsrun
$CLANG/llvm-strip --strip-unneeded build/jsrun
ls -la build/jsrun' 2>&1 | { grep -v "requested image" || true; }
exit ${PIPESTATUS[0]}
