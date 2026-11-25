#!/bin/bash
# Build the native app with the project's clang + the BB10 SDK (gcc 4.8.3 libstdc++, Qt 4.8, Cascades).
# From bb10-tools/: native-tube/build.sh
#   -> native-tube/build/tube   the app: Cascades launcher + the core (core/*.c) in one process
#   -> native-tube/build/tubed  the core alone, for tests over ssh
set -o pipefail
cd "$(dirname "$0")/.."
DOCKER_CONTEXT=colima-aosp docker run --rm -v a11-src:/src -v $PWD/phase6/qnxsdk:/qnxsdk:ro -v $PWD/phase6:/w -v $PWD/native-tube:/t -w /t a11-build bash -c '
. /w/toolchain/qnx-env.sh
set -e
mkdir -p build/core
for f in util media net yt sabr dl server; do
  $QNX_CC -std=gnu11 -O2 -Wall -Wextra -D_GNU_SOURCE -c core/$f.c -o build/core/$f.o &
done
$QNX_CC -std=gnu11 -O2 -Wall -D_GNU_SOURCE -c core/core.c -o build/core/core.o &
$QNX_CC -std=gnu11 -O2 -Wall -D_GNU_SOURCE -DCORE_MAIN -c core/core.c -o build/core/core_main.o &
wait
CORE="build/core/util.o build/core/media.o build/core/net.o build/core/yt.o build/core/sabr.o build/core/dl.o build/core/server.o"
CORELIBS="-lcurl -lcrypto -lsocket -lm"
$QNX_LD $QNX_LDFLAGS_EXE build/core/core_main.o $CORE $CORELIBS $QNX_LIBS_EXE -o build/tubed
GXX=$QT/usr/include/c++/4.8.3
INC="-nostdinc++ -isystem $GXX -isystem $GXX/arm-unknown-nto-qnx8.0.0eabi -isystem $GXX/backward $QNX_SYSINC -isystem $QT/usr/include/qt4 -isystem $QT/usr/include/qt4/QtCore -isystem $QT/usr/include/qt4/QtDeclarative -isystem $QT/usr/include/qt4/QtGui"
$QNX_CXX -std=gnu++98 -O2 -Wall -fvisibility=hidden -DQT_NO_DEBUG -DQT_DECLARATIVE_LIB -DQT_CORE_LIB $INC -c src/main.cpp -o build/main.o
$QNX_LD $QNX_LDFLAGS_EXE -L$QT/armle-v7/usr/lib/qt4/lib -L$QT/armle-v7/lib/gcc/4.8.3 -rpath-link=$QT/armle-v7/usr/lib/qt4/lib build/main.o build/core/core.o $CORE -lbbcascades -lbbmultimedia -lQtDeclarative -lQtCore -lscreen -lstdc++ $CORELIBS $QNX_LIBS_EXE -o build/tube
$CLANG/llvm-strip --strip-unneeded build/tube build/tubed
ls -la build/tube build/tubed; $CLANG/llvm-readelf -d build/tubed | grep NEEDED' 2>&1 | { grep -v "requested image" || true; }
exit ${PIPESTATUS[0]}
