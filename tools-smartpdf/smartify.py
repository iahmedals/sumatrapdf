#!/usr/bin/env python3
"""Make a schematic PDF "smart" for the smartpdf fork of SumatraPDF.

Finds net-name-looking words in the PDF (by default: ALL-CAPS tokens of 3+
characters made of A-Z, 0-9 and _, e.g. CLK, CLK_EN, DATA0) and covers every
occurrence with an invisible link annotation search:<NET_NAME>. Clicking one
in the smartpdf viewer triggers an in-app whole-word, case-sensitive text
search for that name.

Usage:
    python smartify.py input.pdf                  -> writes input-smart.pdf
    python smartify.py input.pdf -o out.pdf       -> writes out.pdf
    python smartify.py input.pdf --all            -> also link names that
                                                     appear only once
    python smartify.py input.pdf --pattern REGEX  -> custom net-name pattern

By default only names appearing at least twice get links (a net connects at
least two points; one-off words are usually title-block text).

Requires: pymupdf  (pip install pymupdf)
"""

import argparse
import collections
import re
import sys
import urllib.parse

try:
    import fitz  # pymupdf
except ImportError:
    sys.exit("pymupdf is not installed. Run:  pip install pymupdf")

DEFAULT_PATTERN = r"[A-Z][A-Z0-9_]{2,}"


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("input", help="PDF to smartify")
    ap.add_argument("-o", "--output", help="output path (default: <input>-smart.pdf)")
    ap.add_argument("--pattern", default=DEFAULT_PATTERN,
                    help=f"regex a word must fully match to count as a net name "
                         f"(default: {DEFAULT_PATTERN})")
    ap.add_argument("--all", action="store_true",
                    help="link names that appear only once too")
    ap.add_argument("--skip-bottom", type=float, default=0.15, metavar="FRACTION",
                    help="ignore words in the bottom FRACTION of each page, where "
                         "the title block and legal boilerplate live "
                         "(default: 0.15; use 0 to disable)")
    args = ap.parse_args()

    out = args.output or re.sub(r"\.pdf$", "", args.input, flags=re.I) + "-smart.pdf"
    rx = re.compile(args.pattern)

    doc = fitz.open(args.input)

    # pass 1: collect candidate words and where they are
    # word tuples: (x0, y0, x1, y1, text, block, line, word_index)
    occurrences = collections.defaultdict(list)  # name -> [(page_no, rect)]
    for pno, page in enumerate(doc):
        y_cutoff = page.rect.height * (1.0 - args.skip_bottom)
        for x0, y0, x1, y1, word, *_ in page.get_text("words"):
            if y0 >= y_cutoff:
                continue  # title block / legal boilerplate strip
            if rx.fullmatch(word):
                occurrences[word].append((pno, fitz.Rect(x0, y0, x1, y1)))

    # pass 2: add a search: link over each occurrence
    min_count = 1 if args.all else 2
    linked_names = 0
    linked_spots = 0
    for name, spots in sorted(occurrences.items()):
        if len(spots) < min_count:
            continue
        uri = "search:" + urllib.parse.quote(name, safe="")
        for pno, rect in spots:
            doc[pno].insert_link({"kind": fitz.LINK_URI, "from": rect, "uri": uri})
            linked_spots += 1
        linked_names += 1

    doc.save(out)
    print(f"{args.input}: {linked_names} net names, {linked_spots} clickable spots")
    print(f"wrote {out}")


if __name__ == "__main__":
    main()
