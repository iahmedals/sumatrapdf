#!/usr/bin/env python3
"""Generate smartpdf-test.pdf: a two-page test document for the smartpdf
click-to-search feature (search: link URIs handled in LinkHandler::LaunchURL).

Each "net label" carries a URI link annotation of the form search:<NET_NAME>,
mimicking what smartify.py stamps onto real schematics. The layout is designed
to exercise: whole-word matching (CLK vs CLK_EN/CLK_OUT/CLKX), match-case
(clk must not be hit), cross-page jumps (RESET), F3 cycling (CLK on both
pages), URL-decoding (DATA0 link written with %-encoding), a not-found term
(MISSING_NET), and an ordinary https: link that must still open the browser.

Usage: python3 make_test_pdf.py [output.pdf]
Requires: reportlab (pip install reportlab)
"""

import sys

from reportlab.lib.pagesizes import letter
from reportlab.pdfgen import canvas

PAGE_W, PAGE_H = letter


def label_with_link(c, x, y, text, uri, size=14):
    """Draw a boxed net label and cover it with a link annotation."""
    c.setFont("Courier-Bold", size)
    w = c.stringWidth(text, "Courier-Bold", size)
    pad = 4
    rect = (x - pad, y - pad, x + w + pad, y + size + pad)
    c.setStrokeColorRGB(0.1, 0.3, 0.8)
    c.rect(rect[0], rect[1], rect[2] - rect[0], rect[3] - rect[1])
    c.setFillColorRGB(0.1, 0.3, 0.8)
    c.drawString(x, y, text)
    c.setFillColorRGB(0, 0, 0)
    c.linkURL(uri, rect, relative=0)


def plain(c, x, y, text, size=12, font="Courier"):
    c.setFont(font, size)
    c.drawString(x, y, text)


def page1(c):
    plain(c, 72, PAGE_H - 60, "smartpdf test sheet - page 1 of 2", 16, "Helvetica-Bold")

    y = PAGE_H - 110
    for line in (
        "Click a blue boxed label; the Find box should fill with the net",
        "name and jump to a highlighted match. F3 / Shift+F3 = next / prev.",
        "",
        "1. CLK        -> finds CLK here and on page 2; NEVER lands on",
        "                 CLK_EN, CLK_OUT, CLKX or lowercase clk",
        "2. RESET      -> jumps to RESET on page 2",
        "3. DATA0      -> link is %-encoded; must still find DATA0",
        "4. MISSING_NET-> shows the normal 'not found' message, no crash",
        "5. https link -> still opens the browser (stock behavior)",
    ):
        plain(c, 72, y, line, 10, "Helvetica")
        y -= 14

    # clickable net labels
    label_with_link(c, 100, 480, "CLK", "search:CLK")
    label_with_link(c, 220, 480, "CLK_EN", "search:CLK_EN")
    label_with_link(c, 380, 480, "CLK_OUT", "search:CLK_OUT")
    label_with_link(c, 100, 420, "RESET", "search:RESET")
    # %44 decodes to "D": proves url::DecodeInPlace runs on the term
    label_with_link(c, 260, 420, "DATA0", "search:%44ATA0")
    label_with_link(c, 100, 360, "MISSING_NET", "search:THIS_NET_EXISTS_NOWHERE")

    # ordinary web link: must keep stock behavior
    c.setFont("Helvetica", 11)
    url = "https://www.sumatrapdfreader.org"
    w = c.stringWidth(url, "Helvetica", 11)
    c.setFillColorRGB(0.1, 0.3, 0.8)
    c.drawString(100, 300, url)
    c.setFillColorRGB(0, 0, 0)
    c.linkURL(url, (100, 296, 100 + w, 310), relative=0)

    # decoy occurrences near the real ones (whole-word / match-case traps)
    plain(c, 100, 240, "wire list: CLK_EN CLKX clk CLK_OUT DATA0 DATA01")
    plain(c, 100, 220, "second CLK occurrence on this page: CLK")


def page2(c):
    plain(c, 72, PAGE_H - 60, "smartpdf test sheet - page 2 of 2", 16, "Helvetica-Bold")
    plain(c, 100, PAGE_H - 120, "CLK appears here too (F3 from page 1 lands here): CLK")
    plain(c, 100, PAGE_H - 160, "RESET target line: RESET")
    plain(c, 100, PAGE_H - 200, "more decoys: CLK_EN CLKX clk RESETX reset DATA0")


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else "smartpdf-test.pdf"
    c = canvas.Canvas(out, pagesize=letter)
    c.setTitle("smartpdf test sheet")
    page1(c)
    c.showPage()
    page2(c)
    c.showPage()
    c.save()
    print(f"wrote {out}")


if __name__ == "__main__":
    main()
