#!/usr/bin/env python3
"""Builds output/pdf/OpenEQ-User-Manual.pdf from docs/USER_MANUAL.md and docs/images.

Needs reportlab (and Pillow for PNG pictures). No JUCE dependency. The Markdown manual is the
single source: headings, paragraphs, "- " lists, tables, images and inline code are laid out as
written. A picture that is missing is skipped with a warning, so the PDF builds before the
screenshots exist. Run with --self-test to check the parser helpers.
"""
import re
import sys
from pathlib import Path
from xml.sax.saxutils import escape

from reportlab.lib import colors
from reportlab.lib.pagesizes import A4
from reportlab.lib.styles import ParagraphStyle, getSampleStyleSheet
from reportlab.lib.units import mm
from reportlab.lib.utils import ImageReader
from reportlab.platypus import Image, ListFlowable, ListItem, Paragraph, SimpleDocTemplate, Spacer, Table, TableStyle

ROOT = Path(__file__).resolve().parents[1]
DOC = ROOT / "docs"
SRC = DOC / "USER_MANUAL.md"
OUT = ROOT / "output/pdf/OpenEQ-User-Manual.pdf"
WIDTH = A4[0] - 36 * mm
NAVY = colors.HexColor("#101a2c")
ACCENT = colors.HexColor("#1aa7c4")
PANEL = colors.HexColor("#eef4f7")
GREY = colors.HexColor("#5a6876")

base = getSampleStyleSheet()
BODY = ParagraphStyle("body", parent=base["BodyText"], fontName="Helvetica", fontSize=9.5, leading=13.5, spaceAfter=6)
CELL = ParagraphStyle("cell", parent=BODY, fontSize=8, leading=10.5, spaceAfter=0)
HEAD = ParagraphStyle("head", parent=CELL, fontName="Helvetica-Bold", textColor=colors.white)
H1 = ParagraphStyle("h1", parent=BODY, fontName="Helvetica-Bold", fontSize=22, leading=26, textColor=NAVY, spaceAfter=10)
H2 = ParagraphStyle("h2", parent=BODY, fontName="Helvetica-Bold", fontSize=14, leading=18, textColor=NAVY, spaceBefore=12, spaceAfter=6, keepWithNext=1)
H3 = ParagraphStyle("h3", parent=BODY, fontName="Helvetica-Bold", fontSize=11, leading=14, textColor=ACCENT, spaceBefore=8, spaceAfter=4, keepWithNext=1)
IMAGE_RE = r"!\[[^\]]*\]\(([^)]+)\)"
missing = []


def inline(text):
    """Markdown inline markup to reportlab's mini-HTML."""
    out = escape(text)
    out = re.sub(r"`([^`]+)`", r'<font face="Courier" size="8">\1</font>', out)
    out = re.sub(r"\*\*([^*]+)\*\*", r"<b>\1</b>", out)
    out = re.sub(r"\[([^\]]+)\]\(([^)]+)\)", r"\1", out)
    return out


def picture(rel, width):
    """The image at docs/<rel>, or None (with a warning) when the file is missing or unreadable."""
    path = DOC / rel
    try:
        w, h = ImageReader(str(path)).getSize()
    except Exception as error:  # missing file, or no image library for this format
        print(f"warning: skipping image {path.relative_to(ROOT)}: {error.__class__.__name__}", file=sys.stderr)
        missing.append(rel)
        return None
    return Image(str(path), width=width, height=width * h / w)


def cell(text, style, width):
    m = re.fullmatch(IMAGE_RE, text.strip())
    if m:
        return picture(m.group(1), width - 4 * mm) or Paragraph("", style)
    return Paragraph(inline(text), style)


def table(rows):
    cols = len(rows[0])
    rows = [r + [""] * (cols - len(r)) for r in rows]
    longest = [max(len(r[i]) for r in rows) for i in range(cols)]
    share = [min(max(n, 8), 60) for n in longest]
    widths = [WIDTH * s / sum(share) for s in share]
    data = [[cell(c, HEAD if i == 0 else CELL, widths[j]) for j, c in enumerate(r)] for i, r in enumerate(rows)]
    t = Table(data, colWidths=widths, repeatRows=1)
    t.setStyle(TableStyle([
        ("BACKGROUND", (0, 0), (-1, 0), NAVY),
        ("ROWBACKGROUNDS", (0, 1), (-1, -1), [colors.white, PANEL]),
        ("GRID", (0, 0), (-1, -1), 0.25, colors.HexColor("#c5d0d8")),
        ("VALIGN", (0, 0), (-1, -1), "TOP"),
        ("LEFTPADDING", (0, 0), (-1, -1), 3), ("RIGHTPADDING", (0, 0), (-1, -1), 3),
        ("TOPPADDING", (0, 0), (-1, -1), 2), ("BOTTOMPADDING", (0, 0), (-1, -1), 2),
    ]))
    return t


def parse(lines):
    story, i = [], 0
    while i < len(lines):
        line = lines[i]
        if not line.strip():
            i += 1
        elif line.startswith("#"):
            level = len(line) - len(line.lstrip("#"))
            story.append(Paragraph(inline(line.lstrip("# ")), {1: H1, 2: H2}.get(level, H3)))
            i += 1
        elif line.startswith("|"):
            rows = []
            while i < len(lines) and lines[i].startswith("|"):
                cells = [c.strip() for c in lines[i].strip().strip("|").split("|")]
                if not all(re.fullmatch(r":?-+:?", c) for c in cells):
                    rows.append(cells)
                i += 1
            story += [table(rows), Spacer(1, 6)]
        elif line.startswith("!["):
            image = picture(re.match(IMAGE_RE, line).group(1), WIDTH)
            if image is not None:
                story += [image, Spacer(1, 6)]
            i += 1
        elif line.startswith("- "):
            items = []
            while i < len(lines) and (lines[i].startswith("- ") or lines[i].startswith("  ")):
                if lines[i].startswith("- "):
                    items.append(lines[i][2:])
                else:
                    items[-1] += " " + lines[i].strip()
                i += 1
            story.append(ListFlowable([ListItem(Paragraph(inline(t), BODY), leftIndent=12) for t in items],
                                      bulletType="bullet", start="•", leftIndent=12))
        else:
            para = []
            while i < len(lines) and lines[i].strip() and not re.match(r"[#|!-] ?", lines[i][:2]):
                para.append(lines[i].strip())
                i += 1
            if not para:  # a line that starts like markup but is plain text
                para, i = [line.strip()], i + 1
            story.append(Paragraph(inline(" ".join(para)), BODY))
    return story


def build():
    story = parse(SRC.read_text(encoding="utf-8").split("\n"))

    def footer(canvas, doc):
        canvas.setFont("Helvetica", 8)
        canvas.setFillColor(GREY)
        canvas.drawString(18 * mm, 10 * mm, "OpenEQ 1.0.3 Beta User Manual")
        canvas.drawRightString(A4[0] - 18 * mm, 10 * mm, str(doc.page))

    OUT.parent.mkdir(parents=True, exist_ok=True)
    SimpleDocTemplate(str(OUT), pagesize=A4, leftMargin=18 * mm, rightMargin=18 * mm, topMargin=16 * mm,
                      bottomMargin=18 * mm, title="OpenEQ User Manual", author="Marcos Quirino Deida").build(
        story, onFirstPage=footer, onLaterPages=footer)
    print(OUT.relative_to(ROOT))
    if missing:
        print(f"warning: {len(missing)} image(s) missing from docs/; the PDF was built without them", file=sys.stderr)
    return 0


def self_test():
    assert inline("**a** `b` [c](d) <e>") == '<b>a</b> <font face="Courier" size="8">b</font> c &lt;e&gt;'
    assert picture("images/__no_such_image__.png", WIDTH) is None and missing[-1].endswith("__no_such_image__.png")
    story = parse(["# T", "", "- one", "  two", "", "| a | b |", "|---|---|", "| 1 |", "", "![x](images/__none__.png)", "text"])
    assert len(story) == 5, story  # heading, list, table + spacer, paragraph; the missing image adds nothing
    print("self-test passed")
    return 0


if __name__ == "__main__":
    sys.exit(self_test() if "--self-test" in sys.argv else build())
