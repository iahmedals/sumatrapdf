#!/usr/bin/env python3
"""Generate a design review demo for SmartPDF.

Creates (default folder: review-demo):
  Demo-Board_RevA.pdf      a small 3-sheet "schematic"
  Demo-Board_RevB.pdf      the next revision: parts moved, Q3 removed, values fixed
  review-comments/         comments from two demo reviewers, made on revision A,
                           with replies, statuses, +1s, sheets reviewed, a sign-off
                           and snapshot pictures

Open Demo-Board_RevB.pdf in SmartPDF and press Ctrl+Shift+R:
  - the revision A comments are found again on revision B (found / moved / gone)
  - Review > Compare with Another Revision highlights what changed since A
  - Team > "I'm the designer" makes your PC write Review-Report.xlsx

Usage: python3 make_review_demo.py [output-dir]
Requires: reportlab, pymupdf, pillow
"""

import io
import json
import os
import re
import sys
import time

import pymupdf
from PIL import Image, ImageDraw
from reportlab.lib.pagesizes import landscape, letter
from reportlab.pdfgen import canvas

W, H = landscape(letter)  # 792 x 612 points
PDF_A = "Demo-Board_RevA.pdf"
PDF_B = "Demo-Board_RevB.pdf"
SHEETS = ["POWER SUPPLY", "MCU", "CONNECTORS"]

# parts per sheet: designator -> (value, x, y, w, h, pins-left, pins-right)
# x, y: bottom-left corner in PDF points (y up)
PARTS_A = [
    {
        "Q3": ("SI2301", 90, 400, 40, 40, [], []),
        "U1": ("TPS62130", 300, 330, 120, 110, ["VIN", "EN", "GND"], ["SW", "FB", "PG"]),
        "L1": ("2.2uH", 480, 400, 50, 18, [], []),
        "C1": ("10uF", 210, 260, 14, 30, [], []),
        "C2": ("100nF", 250, 260, 14, 30, [], []),
        "R1": ("100k", 560, 330, 18, 40, [], []),
        "R2": ("22k", 560, 250, 18, 40, [], []),
        "C3": ("22uF", 630, 360, 14, 30, [], []),
    },
    {
        "U2": ("STM32G071", 250, 200, 180, 250, ["NRST", "BOOT0", "OSC_IN", "OSC_OUT"], ["PA9", "PA10", "PB6", "PB7"]),
        "Y1": ("8MHz", 110, 330, 40, 24, [], []),
        "C5": ("22pF", 80, 270, 14, 30, [], []),
        "C6": ("22pF", 140, 270, 14, 30, [], []),
        "R12": ("1k", 520, 400, 18, 40, [], []),
        "R13": ("1k", 570, 400, 18, 40, [], []),
        "TP7": ("TP", 520, 250, 14, 14, [], []),
    },
    {
        "J1": ("USB4105-GF-A", 100, 250, 90, 160, ["VBUS", "CC1", "CC2", "DP", "DN", "GND"], []),
        "D1": ("TPD2E2U06", 300, 330, 30, 30, [], []),
        "J2": ("HDR-2x5", 560, 250, 60, 140, [], []),
    },
]

NETS_A = [
    [("VIN_12V", 40, 470), ("VCC_3V3", 650, 470), ("PGOOD", 450, 300), ("GND", 300, 200)],
    [("I2C_SCL", 620, 455), ("I2C_SDA", 620, 425), ("NRST", 470, 262)],
    [("VBUS", 380, 410), ("USB_DP", 380, 380), ("USB_DN", 380, 350), ("USB_DP", 640, 330)],
]


def revision_b():
    parts = [dict(p) for p in PARTS_A]
    nets = [list(n) for n in NETS_A]
    # sheet 1: Q3 removed, U1 moved, an extra decoupling cap
    del parts[0]["Q3"]
    v, x, y, w, h, pl, pr = parts[0]["U1"]
    parts[0]["U1"] = (v, x + 30, y + 10, w, h, pl, pr)
    parts[0]["C41"] = ("100nF", 290, 260, 14, 30, [], [])
    # sheet 2: crystal moved, load caps and pull-ups fixed
    v, x, y, w, h, pl, pr = parts[1]["Y1"]
    parts[1]["Y1"] = (v, x, y - 20, w, h, pl, pr)
    parts[1]["C5"] = ("12pF", 80, 250, 14, 30, [], [])
    parts[1]["C6"] = ("12pF", 140, 250, 14, 30, [], [])
    parts[1]["R12"] = ("4.7k", 520, 400, 18, 40, [], [])
    parts[1]["R13"] = ("4.7k", 570, 400, 18, 40, [], [])
    # sheet 3: connector nudged
    v, x, y, w, h, pl, pr = parts[2]["J1"]
    parts[2]["J1"] = (v, x + 10, y, w, h, pl, pr)
    return parts, nets


def draw_sheet(c, idx, parts, nets, rev):
    c.setLineWidth(1)
    c.setStrokeColorRGB(0.55, 0.1, 0.1)
    c.rect(20, 20, W - 40, H - 40)
    # title block (bottom right)
    c.rect(470, 25, 297, 75)
    c.setFillColorRGB(0, 0, 0)
    c.setFont("Helvetica-Bold", 11)
    c.drawString(480, 80, "TITLE: " + SHEETS[idx])
    c.setFont("Helvetica", 8)
    c.drawString(480, 63, "PROJECT: DEMO-BOARD (generated demo, not a real design)")
    c.drawString(480, 48, "REV: " + rev)
    c.drawString(480, 33, "SHEET %d OF %d" % (idx + 1, len(SHEETS)))

    c.setStrokeColorRGB(0.1, 0.1, 0.5)
    for des, (value, x, y, w, h, pins_l, pins_r) in parts.items():
        c.setLineWidth(1.2)
        c.rect(x, y, w, h)
        c.setFont("Helvetica-Bold", 9)
        c.drawString(x, y + h + 4, des)
        c.setFont("Helvetica", 8)
        c.drawString(x, y - 11, value)
        c.setFont("Helvetica", 7)
        for i, p in enumerate(pins_l):
            py = y + h - 18 - i * 22
            c.line(x - 14, py + 2, x, py + 2)
            c.drawString(x + 4, py, p)
        for i, p in enumerate(pins_r):
            py = y + h - 18 - i * 22
            c.line(x + w, py + 2, x + w + 14, py + 2)
            c.drawRightString(x + w - 4, py, p)
    c.setStrokeColorRGB(0.1, 0.45, 0.1)
    for name, x, y in nets:
        c.setFont("Helvetica-Bold", 8)
        tw = c.stringWidth(name, "Helvetica-Bold", 8)
        c.line(x - 18, y + 3, x - 4, y + 3)
        c.rect(x - 3, y - 3, tw + 6, 12)
        c.drawString(x, y, name)


def make_pdf(path, parts, nets, rev):
    c = canvas.Canvas(path, pagesize=(W, H))
    c.setTitle("Demo board rev " + rev)
    for i in range(len(SHEETS)):
        draw_sheet(c, i, parts[i], nets[i], rev)
        c.showPage()
    c.save()


# --- the review comment fingerprint, same rules as src/Review.cpp ---

DESIGNATOR = re.compile(r"^([A-Z]{1,3})([0-9]{1,4})[A-Z]?$")
DESIGNATOR_PREFIXES = set(
    "TP SW FB IC CN BT RN RP RV CR VR DS JP FL MH PS TR LED ANT BZ SP LS TH NTC RLY OSC TVS MK FD FID HS XT ZD "
    "PTC PT ST TC CON SPK MIC BAT XTL REG".split())


def designator_score(w):
    """2: surely a reference designator (R12), 1: maybe (pin PA9), 0: no (SI2301)"""
    m = DESIGNATOR.match(w)
    if not m:
        return 0
    if len(m.group(1)) == 1 or m.group(1) in DESIGNATOR_PREFIXES:
        return 2
    return 1 if len(m.group(2)) <= 2 else 0


def significant(w):
    return len(w) >= 2 and any(ch.isalnum() for ch in w)


def center(r):
    return ((r[0] + r[2]) / 2, (r[1] + r[3]) / 2)


def dist_to_rect_sq(p, r):
    dx = r[0] - p[0] if p[0] < r[0] else (p[0] - r[2] if p[0] > r[2] else 0)
    dy = r[1] - p[1] if p[1] < r[1] else (p[1] - r[3] if p[1] > r[3] else 0)
    return dx * dx + dy * dy


def fingerprint(pt, words):
    best, best_d, best_score = None, 0, 0
    for i, (text, r) in enumerate(words):
        if not significant(text):
            continue
        d = dist_to_rect_sq(pt, r)
        score = designator_score(text)
        limit = 60.0 if score > 0 else 120.0
        if d > limit * limit:
            continue
        if best is None or score > best_score or (score == best_score and d < best_d):
            best, best_d, best_score = i, d, score
    aw, ax, ay = "", 0.0, 0.0
    if best is not None:
        aw = words[best][0]
        cx, cy = center(words[best][1])
        ax, ay = pt[0] - cx, pt[1] - cy
    near = []
    cands = []
    for i, (text, r) in enumerate(words):
        if i == best or not significant(text):
            continue
        cx, cy = center(r)
        d = (cx - pt[0]) ** 2 + (cy - pt[1]) ** 2
        if d <= 150.0 * 150.0:
            cands.append((d, "%s %.1f %.1f" % (text, cx - pt[0], cy - pt[1])))
    for d, e in sorted(cands, key=lambda x: x[0]):
        if e not in near:
            near.append(e)
        if len(near) >= 8:
            break
    return aw, ax, ay, near


def page_words(doc, page_no):
    out = []
    for x0, y0, x1, y1, text, *_ in doc[page_no - 1].get_text("words"):
        out.append((text, (x0, y0, x1, y1)))
    return out


def part_point(parts, page, des, fx=0.5, fy=0.5):
    """A point inside a part (fractions of its box), in page coordinates (y down)."""
    value, x, y, w, h, *_ = parts[page - 1][des]
    return (x + w * fx, H - (y + h * fy))


def snapshot(doc, page, mark, path, color):
    """Small picture of the area around a comment, like the viewer saves."""
    x, y = mark[0], mark[1]
    clip = pymupdf.Rect(x - 130, y - 80, x + 130, y + 80) & doc[page - 1].rect
    zoom = 2.0
    pix = doc[page - 1].get_pixmap(clip=clip, matrix=pymupdf.Matrix(zoom, zoom))
    im = Image.open(io.BytesIO(pix.tobytes("png"))).convert("RGB")
    d = ImageDraw.Draw(im)
    cx, cy = (x - clip.x0) * zoom, (y - clip.y0) * zoom
    if len(mark) == 4:
        d.rectangle([cx, cy, cx + mark[2] * zoom, cy + mark[3] * zoom], outline=color, width=3)
    else:
        r = 10
        d.ellipse([cx - r, cy - r, cx + r, cy + r], outline="white", width=5)
        d.ellipse([cx - r, cy - r, cx + r, cy + r], outline=color, width=3)
    im.save(path)


SEV_COLOR = {"Critical": "#F04438", "Major": "#F97316", "Minor": "#EAB308", "Comment": "#3B82F6"}


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else "review-demo"
    os.makedirs(os.path.join(out, "review-comments", "snapshots"), exist_ok=True)
    parts_b, nets_b = revision_b()
    make_pdf(os.path.join(out, PDF_A), PARTS_A, NETS_A, "A")
    make_pdf(os.path.join(out, PDF_B), parts_b, nets_b, "B")
    doc = pymupdf.open(os.path.join(out, PDF_A))

    now = int(time.time())
    t0 = now - 3 * 86400
    logs = {"demo-ana": [], "demo-ben": []}

    def ev(user, ts, t, **fields):
        e = {"t": t, "by": user, "ts": ts}
        e.update(fields)
        logs[user].append(e)

    ev("demo-ana", t0, "profile", name="Ana Demo", initials="AD")
    ev("demo-ben", t0 + 60, "profile", name="Ben Demo", initials="BD")

    counters = {"demo-ana": 0, "demo-ben": 0}

    def comment(user, ts, page, pt, sev, text, comp="", box=None, shape="pin", net=""):
        counters[user] += 1
        n = counters[user]
        words = page_words(doc, page)
        # like the viewer: a box comment is anchored at its top-left corner
        aw, ax, ay, near = fingerprint((box[0], box[1]) if box else pt, words)
        snap = "%s-%d.png" % (user, n)
        mark = (box[0], box[1], box[2], box[3]) if box else pt
        snapshot(doc, page, mark, os.path.join(out, "review-comments", "snapshots", snap), SEV_COLOR[sev])
        fields = dict(n=n, sev=sev, text=text, rev=PDF_A, page=page, shape=shape, sheet=SHEETS[page - 1],
                      round=1, snap=snap)
        if box:
            fields.update(x=round(box[0], 1), y=round(box[1], 1), w=round(box[2], 1), h=round(box[3], 1))
        else:
            fields.update(x=round(pt[0], 1), y=round(pt[1], 1))
        if comp:
            fields["comp"] = comp
        if net:
            fields["net"] = net
        if aw:
            fields.update(aw=aw, ax=round(ax, 1), ay=round(ay, 1))
        if near:
            fields["near"] = "\n".join(near)
        ev(user, ts, "create", **fields)
        return "%s#%d" % (user, n)

    # sheet 1
    c1 = comment("demo-ana", t0 + 600, 1, part_point(PARTS_A, 1, "U1", 0.15, 0.8), "Critical",
                 "U1 VIN needs a 100nF decoupling cap right at the pin, C1/C2 are too far away.", "U1")
    c2 = comment("demo-ben", t0 + 900, 1, part_point(PARTS_A, 1, "Q3", 0.5, 0.5), "Major",
                 "Q3 reverse polarity FET: Vgs max is 8V but the input can reach 12V. @Ana Demo do we still need it?", "Q3")
    c3 = comment("demo-ana", t0 + 1200, 1, part_point(PARTS_A, 1, "R2", 0.5, 0.5), "Minor",
                 "Check the FB divider: R1/R2 gives 3.36V, not 3.3V.", "R2")
    # sheet 2
    c4 = comment("demo-ana", t0 + 3600, 2, part_point(PARTS_A, 2, "R12", 0.5, 0.5), "Major",
                 "R12/R13 1k pull-ups are too strong for I2C at 100kHz, use 4.7k.", "R12")
    c5 = comment("demo-ben", t0 + 4000, 2, part_point(PARTS_A, 2, "Y1", 0.5, 0.5), "Minor",
                 "Y1 load caps 22pF don't match the crystal's CL = 12pF.", "Y1")
    c6 = comment("demo-ben", t0 + 4300, 2, part_point(PARTS_A, 2, "TP7", 0.5, 0.5), "Comment",
                 "Nice to have: a second test point on BOOT0.", "TP7")
    # sheet 3
    j1 = PARTS_A[2]["J1"]
    box = (j1[1] - 8, H - (j1[2] + j1[4]) - 8, j1[3] + 16, j1[4] + 16)
    c7 = comment("demo-ben", t0 + 7200, 3, (box[0] + box[2] / 2, box[1] + box[3] / 2), "Critical",
                 "J1 USB-C: CC1 and CC2 each need a 5.1k pull-down, otherwise chargers won't give 5V.", "J1",
                 box=box, shape="box")
    usb = [n for n in NETS_A[2] if n[0] == "USB_DP"][0]
    c8 = comment("demo-ana", t0 + 7500, 3, (usb[1] + 15, H - usb[2] - 3), "Comment",
                 "Route USB_DP / USB_DN as a 90 ohm differential pair.", net="USB_DP")
    c9 = comment("demo-ana", t0 + 7800, 3, part_point(PARTS_A, 3, "D1", 0.5, 0.5), "Minor",
                 "Rename net VBUS to VBUS_5V for consistency with the power sheet.", "D1")

    # discussion
    ev("demo-ben", t0 + 8000, "reply", id=c1, rid="demo-ben-1", text="Agreed, adding C41 next to U1 in rev B.")
    ev("demo-ana", t0 + 8100, "agree", id=c7, on=True)
    ev("demo-ana", t0 + 8200, "reply", id=c2, rid="demo-ana-1",
       text="@Ben Demo the input is protected upstream now, Q3 can go.")
    ev("demo-ana", t0 + 8300, "status", id=c5, status="Fixed")
    ev("demo-ben", t0 + 8400, "status", id=c9, status="Rejected")
    ev("demo-ben", t0 + 8410, "reply", id=c9, rid="demo-ben-2", text="We keep VBUS, that's the USB spec name.")
    ev("demo-ben", t0 + 8500, "edit", id=c6, sev="Minor")
    # progress
    for p in (1, 2):
        ev("demo-ana", t0 + 9000 + p, "page", page=p, done=True, sheet=SHEETS[p - 1])
    for p in (1, 3):
        ev("demo-ben", t0 + 9100 + p, "page", page=p, done=True, sheet=SHEETS[p - 1])
    ev("demo-ana", t0 + 9200, "check", item="Power: decoupling and bulk capacitors", done=True)
    ev("demo-ana", t0 + 9201, "check", item="Pull-ups / pull-downs on open-drain and strap pins", done=True)
    ev("demo-ben", t0 + 9300, "check", item="ESD / TVS protection on external connectors", done=True)
    ev("demo-ben", t0 + 9301, "check", item="Connector pinouts verified", done=True)
    ev("demo-ben", t0 + 9400, "signoff", done=True, note="Connectors and power input checked.")
    ev("demo-ana", t0 + 9500, "config", key="assign:demo-ana", val="1-2")
    ev("demo-ana", t0 + 9501, "config", key="assign:demo-ben", val="3")

    for user, events in logs.items():
        with open(os.path.join(out, "review-comments", user + ".jsonl"), "w", encoding="utf-8", newline="\n") as f:
            for e in events:
                f.write(json.dumps(e, ensure_ascii=False) + "\n")
    _ = (c3, c4, c8)
    print("wrote", out)


if __name__ == "__main__":
    main()
