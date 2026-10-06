#!/usr/bin/env python3
"""Make a schematic PDF "smart" for the smartpdf fork of SumatraPDF.

Scans the PDF's words and turns three kinds of tokens into clickable links:

1. Company part numbers (default: yyy-xxxx, e.g. 214-0034)
   -> folder:<parts library path>   (viewer opens File Explorer there)
2. Manufacturer part numbers (letters+digits mix, 7+ chars, no underscore,
   e.g. GRM155R71C104KA88D)
   -> https search for "<pn> datasheet"   (any viewer opens the browser)
3. Net names (ALL-CAPS tokens of 3+ chars: A-Z 0-9 _, e.g. CLK, VREG_S4A_1P8)
   -> search:<name>?z=<zoom>   (viewer runs whole-word text search, zoomed)

Also builds a bookmarks sidebar (PDF outline) listing the part numbers, each
entry jumping to that part's first occurrence. Works in any PDF viewer.
(--bookmarks all adds a Nets tree too; --bookmarks none disables the sidebar.)

Usage:
    python smartify.py input.pdf                  -> writes input-smart.pdf
    python smartify.py input.pdf -o out.pdf
    python smartify.py input.pdf --zoom 300       -> stronger zoom on net click
    python smartify.py input.pdf --zoom 0         -> no zoom (v1 behavior)
    python smartify.py input.pdf --all            -> also link nets appearing once
    python smartify.py input.pdf --no-bookmarks

Category patterns and targets are configurable; see --help.

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

NET_PATTERN = r"[A-Z][A-Z0-9_]{2,}"
COMPANY_PN_PATTERN = r"\d{3}-\d{4}"
# letters and digits mixed, 7+ chars, no underscore (nets use underscores)
# and no slash (slashes join paired net labels like TX1/TXLB)
MPN_PATTERN = r"(?=[A-Z0-9\-\.]*[A-Z])(?=[A-Z0-9\-\.]*\d)[A-Z0-9\-\.]{7,}"
PARTS_DIR_TEMPLATE = (
    r"\\datastore\groups\Engineering\Approved Component\{prefix}\{pn}\Data Sheets"
)
SEARCH_URL_TEMPLATE = "https://www.google.com/search?q={pn}+datasheet"


def build_uri(category, name, args):
    if category == "company":
        prefix = name.split("-")[0]
        path = args.parts_dir_template.format(prefix=prefix, pn=name)
        return "folder:" + urllib.parse.quote(path, safe="")
    if category == "mpn":
        return args.search_url.format(pn=urllib.parse.quote(name, safe=""))
    # net or refdes: in-viewer whole-word search, optionally zoomed
    uri = "search:" + urllib.parse.quote(name, safe="")
    if args.zoom > 0:
        uri += f"?z={args.zoom}"
    return uri


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("input", help="PDF to smartify")
    ap.add_argument("-o", "--output", help="output path (default: <input>-smart.pdf)")
    ap.add_argument("--zoom", type=int, default=200, metavar="PERCENT",
                    help="zoom level applied when a net link is clicked "
                         "(default: 200; 0 disables zooming)")
    ap.add_argument("--pattern", default=NET_PATTERN,
                    help=f"net-name regex (default: {NET_PATTERN})")
    ap.add_argument("--company-pn-pattern", default=COMPANY_PN_PATTERN,
                    help=f"company part-number regex (default: {COMPANY_PN_PATTERN})")
    ap.add_argument("--mpn-pattern", default=MPN_PATTERN,
                    help="manufacturer part-number regex (default: letters+digits "
                         "mix, 7+ chars, no underscore)")
    ap.add_argument("--parts-dir-template", default=PARTS_DIR_TEMPLATE,
                    help="folder opened for a company part number; {prefix} = part "
                         f"of the number before the first dash, {{pn}} = the full "
                         f"number (default: {PARTS_DIR_TEMPLATE})")
    ap.add_argument("--search-url", default=SEARCH_URL_TEMPLATE,
                    help="web search opened for a manufacturer part number; {pn} "
                         f"is replaced (default: {SEARCH_URL_TEMPLATE})")
    ap.add_argument("--all", action="store_true",
                    help="link net names that appear only once too")
    ap.add_argument("--skip-bottom", type=float, default=0.15, metavar="FRACTION",
                    help="ignore words in the bottom FRACTION of each page, where "
                         "the title block and legal boilerplate live "
                         "(default: 0.15; use 0 to disable)")
    ap.add_argument("--bookmarks", choices=("parts", "all", "none"), default="parts",
                    help="bookmarks sidebar content: 'parts' = part numbers only "
                         "(default), 'all' = parts and nets, 'none' = no sidebar")
    ap.add_argument("--nets", metavar="NETS_TXT",
                    help="netlist file with one net name per line (export from any "
                         "EDA tool); when given, ONLY these exact names become net "
                         "links - no pattern guessing, no false positives")
    ap.add_argument("--bom", metavar="BOM_CSV",
                    help="BOM csv with reference-designator and manufacturer "
                         "part-number columns (header auto-detected); when given, "
                         "MPN links come from the BOM instead of pattern guessing, "
                         "and reference designators (R45, U12) become search links")
    args = ap.parse_args()

    known_nets = None
    if args.nets:
        with open(args.nets, encoding="utf-8", errors="replace") as f:
            known_nets = {line.strip() for line in f if line.strip()}

    known_mpns = None
    known_refdes = None
    if args.bom:
        import csv as csv_mod
        with open(args.bom, newline="", encoding="utf-8", errors="replace") as f:
            rows = list(csv_mod.reader(f))
        if not rows:
            sys.exit(f"{args.bom}: empty file")
        header = [h.strip().lower() for h in rows[0]]

        def find_col(*keys):
            for i, h in enumerate(header):
                if any(k in h for k in keys):
                    return i
            return None

        ref_col = find_col("refdes", "reference", "designator", "ref des", "ref")
        mpn_col = find_col("mpn", "manufacturer part", "mfr part", "mfg part", "part number", "part no", "partnum")
        if ref_col is None and mpn_col is None:
            sys.exit(f"{args.bom}: couldn't find refdes or part-number columns in header: {rows[0]}")
        known_mpns = set()
        known_refdes = set()
        for row in rows[1:]:
            if mpn_col is not None and mpn_col < len(row) and row[mpn_col].strip():
                known_mpns.add(row[mpn_col].strip())
            if ref_col is not None and ref_col < len(row) and row[ref_col].strip():
                # refdes cells often hold several: "R1, R2 R7"
                for rd in re.split(r"[,;\s]+", row[ref_col].strip()):
                    if rd:
                        known_refdes.add(rd)

    out = args.output or re.sub(r"\.pdf$", "", args.input, flags=re.I) + "-smart.pdf"
    rx_net = re.compile(args.pattern)
    rx_company = re.compile(args.company_pn_pattern)
    rx_mpn = re.compile(args.mpn_pattern)

    doc = fitz.open(args.input)

    # pass 1: collect and classify words; first matching category wins
    # occurrences[category][name] -> [(page_no, rect), ...]
    occurrences = {
        "company": collections.defaultdict(list),
        "mpn": collections.defaultdict(list),
        "refdes": collections.defaultdict(list),
        "net": collections.defaultdict(list),
    }
    for pno, page in enumerate(doc):
        y_cutoff = page.rect.height * (1.0 - args.skip_bottom)
        for x0, y0, x1, y1, word, *_ in page.get_text("words"):
            if y0 >= y_cutoff:
                continue  # title block / legal boilerplate strip
            # classification; exact knowledge from --nets/--bom replaces guessing
            if rx_company.fullmatch(word):
                cat = "company"
            elif known_mpns is not None and word in known_mpns:
                cat = "mpn"
            elif known_refdes is not None and word in known_refdes:
                cat = "refdes"  # a component: clicking searches for it like a net
            elif known_mpns is None and rx_mpn.fullmatch(word):
                cat = "mpn"
            elif known_nets is not None:
                if word not in known_nets:
                    continue
                cat = "net"
            elif rx_net.fullmatch(word):
                cat = "net"
            else:
                continue
            occurrences[cat][word].append((pno, fitz.Rect(x0, y0, x1, y1)))

    # pass 2: add links. Nets need 2+ occurrences (a net connects at least two
    # points); part numbers are worth clicking even when they appear once.
    counts = collections.Counter()
    net_min = 1 if args.all else 2
    for cat, names in occurrences.items():
        for name, spots in sorted(names.items()):
            if cat == "net" and len(spots) < net_min:
                continue
            uri = build_uri(cat, name, args)
            for pno, rect in spots:
                doc[pno].insert_link({"kind": fitz.LINK_URI, "from": rect, "uri": uri})
                counts[cat + "_spots"] += 1
            counts[cat] += 1

    # pass 3: bookmarks sidebar (first occurrence per name)
    n_bookmarks = 0
    if args.bookmarks != "none":
        toc = []

        def add_section(title, names):
            nonlocal n_bookmarks
            entries = []
            for name, spots in sorted(names.items()):
                if title == "Nets" and len(spots) < net_min:
                    continue
                pno, rect = spots[0]
                entries.append([2, name, pno + 1,
                                {"kind": fitz.LINK_GOTO, "to": fitz.Point(rect.x0, rect.y0)}])
            if entries:
                toc.append([1, title, entries[0][2]])
                toc.extend(entries)
                n_bookmarks += len(entries)

        if args.bookmarks == "all":
            add_section("Nets", occurrences["net"])
        parts = dict(occurrences["company"])
        parts.update(occurrences["mpn"])
        add_section("Parts", parts)
        add_section("Components", occurrences["refdes"])
        if toc:
            doc.set_toc(toc)

    doc.save(out)
    print(f"{args.input}:")
    print(f"  nets:        {counts['net']} names, {counts['net_spots']} clickable spots"
          f" (zoom {args.zoom or 'off'})")
    print(f"  mfr parts:   {counts['mpn']} names, {counts['mpn_spots']} clickable spots"
          f"{' (from BOM)' if known_mpns is not None else ''}")
    if known_refdes is not None:
        print(f"  components:  {counts['refdes']} refdes, {counts['refdes_spots']} clickable spots")
    print(f"  company PNs: {counts['company']} names, {counts['company_spots']} clickable spots")
    print(f"  bookmarks:   {n_bookmarks}")
    print(f"wrote {out}")


if __name__ == "__main__":
    main()
