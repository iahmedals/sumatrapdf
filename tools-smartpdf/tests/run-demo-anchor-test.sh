#!/bin/bash
# Re-anchoring test on the review demo: generates the demo (two revisions + comments made
# on revision A), extracts revision B's words and checks that every comment is found /
# moved / gone as expected. Needs: python3 with reportlab, pymupdf, pillow; clang++.
# Run tools-smartpdf/tests/run-review-tests.sh first (it builds the base library objects).
set -e
cd "$(dirname "$0")/../.."
OUT=${1:-out/review-tests}
DEMO="$OUT/demo"
rm -rf "$DEMO"
python3 tools-smartpdf/make_review_demo.py "$DEMO" >/dev/null
python3 - "$DEMO/Demo-Board_RevB.pdf" "$OUT/demo-wordsB.tsv" <<'PY'
import sys, pymupdf
with open(sys.argv[2], "w") as f:
    for i, p in enumerate(pymupdf.open(sys.argv[1])):
        for x0, y0, x1, y1, t, *_ in p.get_text("words"):
            f.write("%d\t%s\t%.2f\t%.2f\t%.2f\t%.2f\n" % (i + 1, t, x0, y0, x1, y1))
PY
FLAGS="-O0 -g -DDEBUG -include climits -Isrc -Iext/libarchive -std=c++23 -D__GXX_TYPEINFO_EQUALITY_INLINE=1 -fno-rtti -fno-exceptions"
BASE="Base.cpp Base_posix.cpp Arena.cpp Arena_posix.cpp ByteOrderDecoder.cpp ByteReader.cpp Color.cpp DbgHelpDyn_posix.cpp DirScan.cpp DirScan_posix.cpp Dpi_posix.cpp File.cpp File_posix.cpp Geom.cpp JsonParser.cpp StrQueue.cpp Thread.cpp WinDynCalls_posix.cpp Str.cpp StrUtf8.cpp StrFormatParse.cpp StrVec.cpp Strconv.cpp"
BOBJS=""; for f in $BASE; do BOBJS="$BOBJS $OUT/obj/base_${f%.cpp}.o"; done
${CXX:-clang++} $FLAGS -w tools-smartpdf/tests/demo_anchor_check.cpp src/Review.cpp src/CrashHandlerNoOp.cpp $BOBJS -lpthread -o "$OUT/demo_anchor_check"
got=$("$OUT/demo_anchor_check" "$OUT/demo-wordsB.tsv" "$DEMO"/review-comments/*.jsonl)
echo "$got"
expect="demo-ana#1 U1 found 348 184
demo-ben#1 Q3 gone 0 0
demo-ana#2 R2 found 569 342
demo-ana#3 R12 found 529 192
demo-ben#2 Y1 moved 130 290
demo-ben#3 TP7 found 527 355
demo-ben#4 J1 found 102 194
demo-ana#4 USB_DP found 395 229
demo-ana#5 D1 found 315 267"
if [ "$got" == "$expect" ]; then echo "demo anchor test: OK"; else echo "demo anchor test: FAILED"; exit 1; fi
