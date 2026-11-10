#!/usr/bin/env bash
#
# mayhem/build.sh — build rtty's fuzz harnesses and its selftest oracle.
#
# rtty's CMake build needs its git submodules (src/buffer, src/log), which are empty gitlinks in the
# fork, so this compiles the sources directly. The submodules are pre-fetched into
# /opt/toolchains/deps/ by mayhem/Dockerfile at the commits pinned in .gitmodules, so the build is
# offline. SSL support is off (it is optional upstream and needs the ssl submodule's wolfssl/openssl).
set -euo pipefail

[ -n "${SOURCE_DATE_EPOCH:-}" ] || unset SOURCE_DATE_EPOCH

: "${SANITIZER_FLAGS=-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer}"
: "${DEBUG_FLAGS:=-g -gdwarf-3}"
: "${CC:=clang}" ; : "${CXX:=clang++}" ; : "${LIB_FUZZING_ENGINE:=-fsanitize=fuzzer}"
: "${MAYHEM_JOBS:=$(nproc)}"
: "${COVERAGE_FLAGS=}"
: "${STANDALONE_FUZZ_MAIN:=/opt/mayhem/StandaloneFuzzTargetMain.c}"
export SANITIZER_FLAGS DEBUG_FLAGS CC CXX LIB_FUZZING_ENGINE MAYHEM_JOBS COVERAGE_FLAGS

cd "$SRC"

BUFFER_REV=1237374586ab48c069efce1810b130ef858cfca2
LOG_REV=1aa7ee85fcad12d440e2fbed3b725722ca189f6b
BUF=/opt/toolchains/deps/buffer-$BUFFER_REV/src
LOGSRC=/opt/toolchains/deps/log-$LOG_REV/src

# Build outputs live under build/ (gitignored upstream), so a `git clean -ffdX` starts from scratch.
B="$SRC/build/mayhem"
rm -rf "$B"
mkdir -p "$B/inc/log" "$B/fuzz" "$B/test"

# What cmake would generate: src/config.h (version from src/CMakeLists.txt, no SSL_SUPPORT) and the
# "log/log.h" include path that the empty src/log submodule directory would have provided.
cp "$LOGSRC/log.h" "$B/inc/log/log.h"
ver() { sed -n "s/^set(RTTY_VERSION_$1 \([0-9]*\))/\1/p" src/CMakeLists.txt; }
sed -e "s/@RTTY_VERSION_MAJOR@/$(ver MAJOR)/g" -e "s/@RTTY_VERSION_MINOR@/$(ver MINOR)/g" \
    -e "s/@RTTY_VERSION_PATCH@/$(ver PATCH)/g" -e '/#cmakedefine/d' src/config.h.in > "$B/inc/config.h"

INC="-I$B/inc -I$BUF -I$SRC/src -I$SRC/mayhem"
BASE="-D_GNU_SOURCE -std=gnu99 -O1 -include $SRC/mayhem/mock.h $INC"

# Sources shared by every harness. rtty.c / command.c / main.c are included into the harness or selftest
# translation unit that needs their static functions, and linked as objects for the others.
COMMON="$BUF/buffer.c $LOGSRC/log.c src/utils.c src/term.c src/serial.c src/tcp.c src/net.c src/http.c src/file.c src/fileipc.c mayhem/mock.c"
OTHERS="src/rtty.c src/command.c src/filectl.c"
LIBS="-lev -linih -lutil -lm"

compile_set() { # <outdir> <flags> <sources...>
    local out="$1" flags="$2" f; shift 2
    for f in "$@"; do
        $CC $flags -c "$f" -o "$out/$(basename "$f" .c).o"
    done
}

# 1) Fuzz objects: the project itself, sanitized, DWARF 3.
# fuzzer-no-link adds the SanCov instrumentation to the project and harness objects; the engine is linked per harness.
FUZZ_FLAGS="$SANITIZER_FLAGS -fsanitize=fuzzer-no-link $DEBUG_FLAGS $BASE"
compile_set "$B/fuzz" "$FUZZ_FLAGS" $COMMON $OTHERS
$CXX $SANITIZER_FLAGS -c mayhem/lsan_off.cc -o "$B/fuzz/lsan_off.o"
$CC $SANITIZER_FLAGS $DEBUG_FLAGS -c "$STANDALONE_FUZZ_MAIN" -o "$B/fuzz/standalone_main.o"

objs() { # <dir> <names...>
    local d="$1" n; shift
    for n in "$@"; do printf '%s/%s.o ' "$d" "$n"; done
}
COMMON_O=$(objs "$B/fuzz" buffer log utils term serial tcp net http file fileipc mock)

link_pair() { # <harness> <extra objects...>
    local h="$1"; shift
    $CC $FUZZ_FLAGS -c "mayhem/$h.c" -o "$B/fuzz/$h.o"
    $CC $SANITIZER_FLAGS $DEBUG_FLAGS $LIB_FUZZING_ENGINE "$B/fuzz/$h.o" "$@" "$B/fuzz/lsan_off.o" $LIBS -o "$SRC/$h"
    $CC $SANITIZER_FLAGS $DEBUG_FLAGS "$B/fuzz/standalone_main.o" "$B/fuzz/$h.o" "$@" "$B/fuzz/lsan_off.o" $LIBS -o "$SRC/$h-standalone"
}

link_pair fuzz_parse_msg  $COMMON_O "$B/fuzz/filectl.o"
link_pair fuzz_run_command $COMMON_O "$B/fuzz/rtty.o" "$B/fuzz/filectl.o"
link_pair fuzz_file_msg   $COMMON_O "$B/fuzz/rtty.o" "$B/fuzz/command.o" "$B/fuzz/filectl.o"
link_pair fuzz_config     $COMMON_O "$B/fuzz/rtty.o" "$B/fuzz/command.o" "$B/fuzz/filectl.o"

# 2) The selftest (mayhem/test.sh's oracle), built with the project's NORMAL flags: no sanitizers.
TEST_FLAGS="-g0 $BASE $COVERAGE_FLAGS"
compile_set "$B/test" "$TEST_FLAGS" $COMMON src/filectl.c
$CC $TEST_FLAGS -c mayhem/rtty_selftest.c -o "$B/test/rtty_selftest.o"
$CC $TEST_FLAGS -c mayhem/rtty_selftest_config.c -o "$B/test/rtty_selftest_config.o"
$CC $TEST_FLAGS "$B/test/rtty_selftest.o" "$B/test/rtty_selftest_config.o" \
    $(objs "$B/test" buffer log utils term serial tcp net http file fileipc mock filectl) \
    $COVERAGE_FLAGS $LIBS -o "$SRC/rtty_selftest"
