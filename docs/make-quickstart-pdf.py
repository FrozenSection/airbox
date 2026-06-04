#!/usr/bin/env python3
"""Generate AirBox-Quick-Start.pdf (one page, letter) from the quick-start text.

Reproducible build of the printable card that ships with the device. Requires
reportlab (which also provides the QR code):

    python3 -m venv venv && ./venv/bin/pip install reportlab
    ./venv/bin/python docs/make-quickstart-pdf.py

Output: docs/AirBox-Quick-Start.pdf
"""
import os
from reportlab.lib.pagesizes import letter
from reportlab.lib.units import inch
from reportlab.lib import colors
from reportlab.lib.styles import getSampleStyleSheet, ParagraphStyle
from reportlab.platypus import (SimpleDocTemplate, Paragraph, Spacer, Table,
                                TableStyle, HRFlowable)
from reportlab.graphics.shapes import Drawing
from reportlab.graphics.barcode.qr import QrCodeWidget

REPO = "https://github.com/FrozenSection/airbox"
ACCENT = colors.HexColor("#2f6df6")
DARK = colors.HexColor("#161b22")
GREY = colors.HexColor("#5b6b7e")
OUT = os.path.join(os.path.dirname(__file__), "AirBox-Quick-Start.pdf")

ss = getSampleStyleSheet()
def st(name, **kw):
    kw.setdefault("fontName", "Helvetica")
    return ParagraphStyle(name, parent=ss["Normal"], **kw)

title  = st("t", fontName="Helvetica-Bold", fontSize=28, textColor=DARK, leading=30)
sub    = st("s", fontSize=13, textColor=ACCENT, leading=16, spaceBefore=1)
desc   = st("d", fontSize=9.5, textColor=GREY, leading=12.5, spaceBefore=4)
hdr    = st("h", fontName="Helvetica-Bold", fontSize=11.5, textColor=ACCENT,
            spaceBefore=11, spaceAfter=3)
body   = st("b", fontSize=9.7, textColor=DARK, leading=13.4)
cap    = st("c", fontSize=7.4, textColor=GREY, leading=9, alignment=1)
foot   = st("f", fontSize=9, textColor=GREY, leading=12, alignment=1)

def step(n, lead, rest):
    return Paragraph(
        f"<font color='#2f6df6'><b>{n}</b></font>&nbsp;&nbsp;<b>{lead}</b> {rest}",
        st("step", fontSize=9.7, textColor=DARK, leading=13.6, spaceAfter=4))

def bullet(txt):
    return Paragraph(f"<font color='#2f6df6'>&bull;</font>&nbsp;&nbsp;{txt}",
                     st("bl", fontSize=9.5, textColor=DARK, leading=13.2, spaceAfter=3,
                        leftIndent=2))

def qr_drawing(size=104):
    w = QrCodeWidget(REPO)
    b = w.getBounds()
    sx, sy = size / (b[2] - b[0]), size / (b[3] - b[1])
    d = Drawing(size, size, transform=[sx, 0, 0, sy, 0, 0])
    d.add(w)
    return d

doc = SimpleDocTemplate(OUT, pagesize=letter,
                        leftMargin=0.62*inch, rightMargin=0.62*inch,
                        topMargin=0.5*inch, bottomMargin=0.45*inch,
                        title="AirBox - Quick Start")
S = []

# --- Header: title block (left) + QR (right) ---
left = [Paragraph("AirBox", title), Paragraph("Quick Start", sub),
        Paragraph("A standalone indoor air &amp; environment monitor. No app, "
                  "account, or cloud - it serves its own dashboard on your "
                  "local network.", desc)]
right = [qr_drawing(), Spacer(1, 2),
         Paragraph("Scan for the full<br/>guide &amp; source", cap)]
head = Table([[left, right]], colWidths=[4.7*inch, 1.55*inch])
head.setStyle(TableStyle([("VALIGN", (0, 0), (0, 0), "TOP"),
                          ("VALIGN", (1, 0), (1, 0), "TOP"),
                          ("ALIGN", (1, 0), (1, 0), "CENTER"),
                          ("LEFTPADDING", (0, 0), (-1, -1), 0),
                          ("RIGHTPADDING", (0, 0), (-1, -1), 0),
                          ("TOPPADDING", (0, 0), (-1, -1), 0)]))
S += [head, Spacer(1, 6),
      HRFlowable(width="100%", thickness=1, color=colors.HexColor("#d7dde5"))]

S += [Paragraph("Set it up", hdr)]
S += [step(1, "Power it on.", "Plug in USB-C. The screen shows <b>WiFi Setup</b>, "
                "the network <b>AirBox-Setup</b>, and a QR code."),
      step(2, "Join the device.", "On your phone, scan that QR to join the "
                "<b>AirBox-Setup</b> WiFi (open, no password). A setup page opens "
                "automatically. (If not, browse to http://192.168.4.1.)"),
      step(3, "Hand it your WiFi.", "Pick your <b>2.4 GHz</b> network, enter its "
                "password, tap <b>Connect</b>. The device reboots and joins."),
      step(4, "Open the dashboard.", "On any device on the same WiFi, go to "
                "<b>http://airbox.local</b> (or the IP shown on the screen).")]

S += [Paragraph("Reading the dashboard", hdr),
      Paragraph("Four live tiles - <b>Temperature, Humidity, Pressure, Air "
                "Quality (IAQ)</b> - each with an 8-hour trend. IAQ is a 0 to 500 "
                "index (lower = cleaner) and needs <b>24-48 h</b> to self-calibrate. "
                "The <b>Diagnostics</b> tab has sensor health, network info, and a "
                "<b>Download CSV</b> button (last 7 days). The <b>Settings</b> tab "
                "covers name, units, time zone, screen brightness / night mode.", body)]

S += [Paragraph("Good to know", hdr),
      bullet("Firmware updates happen in the browser at "
             "<b>http://airbox.local/update</b> (no password needed)."),
      bullet("Enable <b>Night Mode</b> to blank or dim the screen on a schedule; "
             "the dashboard still works while the screen is off.")]

S += [Paragraph("If something seems off", hdr),
      bullet("<b>Can't reach airbox.local</b> - use the IP address shown on the "
             "device screen."),
      bullet("<b>Changing WiFi / moving it</b> - Settings &gt; Reconfigure WiFi, "
             "or hold the internal <b>BOOT</b> button ~3 s. (Air-quality "
             "calibration is preserved.)"),
      bullet("<b>Screen shows STALE</b> - a momentary sensor hiccup; it "
             "self-recovers, or power-cycle it.")]

S += [Spacer(1, 10),
      HRFlowable(width="100%", thickness=1, color=colors.HexColor("#d7dde5")),
      Spacer(1, 5),
      Paragraph("Full guide, data details &amp; source code:  "
                "<b>github.com/FrozenSection/airbox</b>", foot)]

doc.build(S)
print("wrote", OUT)
