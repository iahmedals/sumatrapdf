#!/bin/bash
# Builds and runs the portable smartpdf review tests on Linux:
#   tools-smartpdf/tests/run-review-tests.sh [outdir]
# Compiles src/base (portable subset) + src/Review*.cpp + review_tests.cpp with clang++.
set -e
cd "$(dirname "$0")/../.."
OUT=${1:-out/review-tests}
mkdir -p "$OUT/obj"
CXX=${CXX:-clang++}
FLAGS="-O0 -g -DDEBUG -include climits -Isrc -Iext/libarchive -std=c++23 -D__GXX_TYPEINFO_EQUALITY_INLINE=1 -fno-rtti -fno-exceptions"
BASE="Base.cpp Base_posix.cpp Arena.cpp Arena_posix.cpp ByteOrderDecoder.cpp ByteReader.cpp Color.cpp DbgHelpDyn_posix.cpp DirScan.cpp DirScan_posix.cpp Dpi_posix.cpp File.cpp File_posix.cpp Geom.cpp JsonParser.cpp StrQueue.cpp Thread.cpp WinDynCalls_posix.cpp Str.cpp StrUtf8.cpp StrFormatParse.cpp StrVec.cpp Strconv.cpp"
pids=""
for f in $BASE; do
  o="$OUT/obj/base_${f%.cpp}.o"
  if [ ! -f "$o" ] || [ "src/base/$f" -nt "$o" ]; then
    $CXX $FLAGS -w -c "src/base/$f" -o "$o" & pids="$pids $!"
  fi
done
for p in $pids; do wait $p; done
OWN="src/Review.cpp src/ReviewXlsx.cpp src/CrashHandlerNoOp.cpp tools-smartpdf/tests/review_tests.cpp"
objs=""
for f in $OWN; do
  [ -f "$f" ] || continue
  o="$OUT/obj/$(basename ${f%.cpp}).o"
  $CXX $FLAGS -Wall -Wextra -Wshadow -Wno-unused-parameter -Wno-missing-field-initializers -c "$f" -o "$o"
  objs="$objs $o"
done
BOBJS=""; for f in $BASE; do BOBJS="$BOBJS $OUT/obj/base_${f%.cpp}.o"; done
$CXX -o "$OUT/review_tests" $objs $BOBJS -lpthread
"$OUT/review_tests" "$OUT"
