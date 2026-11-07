"""Builds docs/NFC_Toolkit_User_Guide.pdf from docs/screenshots.  Usage: python3 tools/guide/make_guide.py
Needs: pip install reportlab, and the DejaVu fonts (set DEJAVU_DIR if they live elsewhere)."""
import os
from reportlab.lib.pagesizes import letter
from reportlab.platypus import (SimpleDocTemplate, Paragraph, Spacer, Table, TableStyle, Image,
                                PageBreak, KeepTogether)
from reportlab.lib.styles import getSampleStyleSheet, ParagraphStyle
from reportlab.lib import colors
from reportlab.lib.units import inch
from reportlab.pdfbase import pdfmetrics
from reportlab.pdfbase.ttfonts import TTFont

F = os.environ.get("DEJAVU_DIR", "/usr/share/fonts/truetype/dejavu/")
pdfmetrics.registerFont(TTFont("Sans", F + "DejaVuSans.ttf"))
pdfmetrics.registerFont(TTFont("Sans-Bold", F + "DejaVuSans-Bold.ttf"))
pdfmetrics.registerFont(TTFont("Mono", F + "DejaVuSansMono.ttf"))
from reportlab.lib.fonts import addMapping
addMapping("Sans", 0, 0, "Sans"); addMapping("Sans", 1, 0, "Sans-Bold")
addMapping("Sans", 0, 1, "Sans"); addMapping("Sans", 1, 1, "Sans-Bold")

import os, sys
REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
SHOTS = os.path.join(REPO, "docs", "screenshots") + "/"
ACC = colors.HexColor("#1F6FB2")
INK = colors.HexColor("#1B2430")
DIM = colors.HexColor("#5B6775")
RULE = colors.HexColor("#D5DCE3")
TINT = colors.HexColor("#F1F5F9")

ss = getSampleStyleSheet()
body = ParagraphStyle("body", parent=ss["Normal"], fontName="Sans", fontSize=9.6, leading=13.6, textColor=INK, spaceAfter=5)
small = ParagraphStyle("small", parent=body, fontSize=8.4, leading=11.4, textColor=DIM)
cell = ParagraphStyle("cell", parent=body, fontSize=8.2, leading=10.8, spaceAfter=0)
cellb = ParagraphStyle("cellb", parent=cell, fontName="Sans-Bold")
h1 = ParagraphStyle("h1", parent=body, fontName="Sans-Bold", fontSize=17, leading=21, textColor=ACC, spaceBefore=4, spaceAfter=8)
h2 = ParagraphStyle("h2", parent=body, fontName="Sans-Bold", fontSize=12.5, leading=16, textColor=INK, spaceBefore=10, spaceAfter=4)
bul = ParagraphStyle("bul", parent=body, leftIndent=12, bulletIndent=2, spaceAfter=2.5)
code = ParagraphStyle("code", parent=body, fontName="Mono", fontSize=8.4, leading=11.5, backColor=TINT,
                      borderPadding=(5, 6, 5, 6), spaceBefore=3, spaceAfter=8)
title = ParagraphStyle("title", parent=body, fontName="Sans-Bold", fontSize=30, leading=36, textColor=INK)
sub = ParagraphStyle("sub", parent=body, fontSize=13, leading=18, textColor=DIM)


def P(t, s=body): return Paragraph(t, s)
def B(items): return [Paragraph(i, bul, bulletText="•") for i in items]


def shot(name, w=1.75 * inch):
    return Image(SHOTS + name + ".png", width=w, height=w * 502 / 410)


def table(rows, widths, header=True, zebra=True):
    data = [[P(c, cellb if (header and r == 0) else cell) if isinstance(c, str) else c for c in row]
            for r, row in enumerate(rows)]
    t = Table(data, colWidths=widths, repeatRows=1 if header else 0)
    st = [("VALIGN", (0, 0), (-1, -1), "TOP"),
          ("LINEBELOW", (0, 0), (-1, -1), 0.4, RULE),
          ("TOPPADDING", (0, 0), (-1, -1), 4), ("BOTTOMPADDING", (0, 0), (-1, -1), 4),
          ("LEFTPADDING", (0, 0), (-1, -1), 5), ("RIGHTPADDING", (0, 0), (-1, -1), 5)]
    if header:
        st += [("BACKGROUND", (0, 0), (-1, 0), TINT), ("LINEBELOW", (0, 0), (-1, 0), 0.9, INK)]
    t.setStyle(TableStyle(st))
    return t


def app_section(heading, shots, paras):
    """Heading, then screenshots on the left and instructions on the right."""
    imgs = Table([[shot(s)] for s in shots], colWidths=[1.9 * inch])
    imgs.setStyle(TableStyle([("LEFTPADDING", (0, 0), (-1, -1), 0), ("BOTTOMPADDING", (0, 0), (-1, -1), 6)]))
    t = Table([[imgs, paras]], colWidths=[2.0 * inch, 4.9 * inch])
    t.setStyle(TableStyle([("VALIGN", (0, 0), (-1, -1), "TOP"), ("LEFTPADDING", (0, 0), (-1, -1), 0),
                           ("RIGHTPADDING", (0, 0), (-1, -1), 0)]))
    return [P(heading, h2), t, Spacer(1, 6)]


def on_page(c, doc):
    c.saveState()
    c.setFont("Sans", 7.5); c.setFillColor(DIM)
    if doc.page > 1:
        c.drawString(0.8 * inch, 0.5 * inch, "NFC Toolkit for LilyGO T-Watch Ultra  ·  User guide")
        c.drawRightString(letter[0] - 0.8 * inch, 0.5 * inch, str(doc.page))
    c.restoreState()


s = []
# ------------------------------------------------------------------ cover
s += [Spacer(1, 0.6 * inch), P("NFC Toolkit", title), P("for the LilyGO T-Watch Ultra (ST25R3916)", sub),
      Spacer(1, 4), P("User guide: installing, using every tool, and what it can read, write and emulate", small),
      Spacer(1, 18)]
cover = Table([[shot("01_home", 2.1 * inch), shot("03_scan_result", 2.1 * inch), shot("13_rflab", 2.1 * inch)]],
              colWidths=[2.3 * inch] * 3)
cover.setStyle(TableStyle([("ALIGN", (0, 0), (-1, -1), "CENTER")]))
s += [cover, Spacer(1, 16)]
s.append(P("The T-Watch Ultra contains an ST25R3916, the same NFC front-end STMicroelectronics uses in its "
           "development kits. It can read and write tags, act as a tag itself, sense other NFC readers and "
           "measure its own antenna. NFC Toolkit puts all of that on eight tiles on the watch face."))
s.append(P("The app runs on the T-Watch Ultra. For sharp, glare-free pictures, the screenshots in this guide "
           "were captured from a desktop simulator running the app's real interface code at the watch's exact "
           "410×502 resolution, with sample tag data. They match the layout on the watch.", small))
s.append(PageBreak())

# ------------------------------------------------------------------ contents at a glance
s.append(P("At a glance", h1))
s.append(table([
    ["Tile", "Use it to"],
    ["Scan", "Identify any tag: ID, type and what's stored on it. Turn results into a QR code, join a Wi-Fi network, or emulate the tag."],
    ["Write", "Put a link, text, Wi-Fi login, phone number, SMS, email, contact card, map location or Android app launcher on a tag. Erase or format tags."],
    ["Copy", "Copy what's stored on one tag onto another."],
    ["Dump", "Read a tag's entire memory, byte by byte, and save it to the SD card."],
    ["Emulate", "Make the watch itself act as a tag that phones can tap."],
    ["Field", "Detect phones, payment terminals and door/transit readers that are sending an NFC field."],
    ["RF Lab", "See the NFC chip's health: supply voltages, antenna signal, and a live graph that reacts to nearby tags."],
    ["History", "Review everything you've scanned or dumped, with timestamps."],
], [1.0 * inch, 5.9 * inch]))

s.append(P("Getting around", h2))
s += B(["Tap a tile to open a tool. Tap the <b>&lt;</b> button in the coloured header (or press <b>BOOT</b>) to go back to the home screen.",
        "Long results scroll: swipe up and down inside the screen.",
        "Text boxes open an on-screen keyboard. Tap the tick to close it.",
        "The watch <b>vibrates</b> when a tag is detected, when a write or copy finishes, and when an NFC field appears.",
        "Leaving a tool always switches the NFC radio off, so nothing keeps transmitting in the background."])

s.append(P("Where to hold the tag", h2))
s.append(P("NFC range is only a few centimetres. Hold the tag flat against the watch and move it around slowly until "
           "it beeps (vibrates). Once you find the sweet spot, it reads instantly every time. If you're not sure where "
           "the antenna is, use <b>RF Lab</b>: turn the carrier on, set a baseline, then slide a tag around the watch and "
           "watch where the graph jumps. For phones, the NFC coil is usually near the camera on the back."))

s.append(P("Files and storage", h2))
s.append(P("Scan history and saved dumps go in a folder called <b>/nfc</b>. If a microSD card is inserted, the "
           "app uses it; otherwise it uses the watch's internal storage. History shows which one is in use. "
           "Saved dumps are a readable <font face='Mono'>.txt</font> plus a raw <font face='Mono'>.bin</font>, named "
           "by tag ID and time."))
s.append(PageBreak())

# ------------------------------------------------------------------ tools
s.append(P("Using each tool", h1))

s += app_section("Scan", ["02_scan_idle", "03_scan_result"], [
    P("Open <b>Scan</b> and hold a tag to the watch. The watch vibrates and shows:"),
    *B(["<b>UID</b>: the tag's serial number",
        "<b>Card</b>: the chip model, for example NTAG215 or MIFARE Classic 1K",
        "<b>Technology / interface / tag type</b>: NFC-A, ISO-DEP, Type 2, and so on",
        "<b>NDEF</b>: whether the tag holds standard NFC data, its version, and how full it is",
        "Each stored record: links, text, Wi-Fi name and password, contact cards, app records"]),
    P("Every scan is saved to History automatically. The buttons under the result use what was found:"),
    *B(["<b>Emulate this tag</b> opens Emulate with the link, text or Wi-Fi details filled in.",
        "<b>Show as QR code</b> shows the link (or UID) full-screen so a phone camera can scan it. Tap to close.",
        "<b>Join this Wi-Fi</b> appears when the tag holds a Wi-Fi login and connects the watch to that network."]),
    P("<b>Low-power wake-up</b>: when switched on, the chip keeps its field off and only powers up when something "
      "disturbs the antenna. That saves battery if you leave Scan open. Reads can take a moment longer to start.", small),
])
s.append(PageBreak())

s += app_section("Write", ["05_write_url", "07_write_wifi", "08_write_done"], [
    P("1. Pick <b>What to write</b>. 2. Fill in the boxes. 3. Tap <b>Write to tag</b> and hold a tag to the watch. "
      "The watch writes, reads the data back to check it, then shows <b>Write complete</b> or the reason it failed."),
    table([
        ["Type", "What a phone does when it taps the tag"],
        ["Web link (URL)", "Opens the website. If you leave out https://, it's added for you."],
        ["Text", "Shows the text (or passes it to an NFC app)."],
        ["Wi-Fi network", "Offers to join the network. Choose WPA2, WPA/WPA2 mixed or Open."],
        ["Phone call", "Opens the dialler with the number."],
        ["SMS", "Opens a text message with the number and message filled in."],
        ["Email", "Opens a new email with address, subject and body."],
        ["Contact (vCard)", "Offers to save a contact with name, phone and email."],
        ["Map location", "Opens maps at that latitude and longitude."],
        ["Launch Android app", "Opens (or offers to install) the app with that package name."],
        ["Erase tag", "Replaces the contents with an empty record. The tag stays reusable."],
        ["Format blank tag", "Prepares a brand-new, never-used tag so it can hold NFC data."],
    ], [1.35 * inch, 3.4 * inch]),
    Spacer(1, 6),
    P("Brand-new tags are formatted automatically the first time you write to them. If the message is too big "
      "for the tag, the watch says so instead of writing part of it.", small),
])
s.append(PageBreak())

s += app_section("Copy", ["09_copy"], [
    P("Copies the stored data (links, text, Wi-Fi, contacts) from one tag to another."),
    *B(["Tap <b>1. Read source tag</b> and hold the tag you want to copy. Its contents appear on screen.",
        "Tap <b>2. Write to new tag</b> and hold a different, writable tag. If you hold the original by mistake, "
        "the watch tells you and waits for another tag.",
        "The copy is checked the same way as Write."]),
    P("The two tags don't have to be the same kind, so you can copy an NTAG sticker onto an ISO15693 tag. "
      "Only the contents are copied, not the serial number (UID), which is fixed in each chip at the factory.", small),
])
s += app_section("Dump", ["10_dump", "10b_dump_scrolled"], [
    P("Reads everything the tag will give up and shows it as a hex listing with a text column on the right. "
      "It starts listening as soon as you open it. Hold a tag; when it's done, tap <b>Save</b> to write it to "
      "storage or <b>Again</b> for another tag."),
    table([
        ["Tag", "What the dump contains"],
        ["NTAG21x, Ultralight", "Exact model, every page including configuration, decoded NFC data, and NXP's "
                                "originality signature (proof the chip is genuine NXP)."],
        ["ISO15693 (ICODE, ST25DV, Tag-it)", "System info and every block, including large chips with more than 256 blocks."],
        ["ST25TB / SRI", "All blocks plus the system block."],
        ["FeliCa Lite / Lite-S", "The readable (unencrypted) blocks."],
        ["Type 4 / DESFire / NTAG 424", "The NFC data file and its settings."],
        ["Bank and transit cards", "Only the names and IDs of the payment apps on the card. No card numbers or transactions."],
        ["MIFARE Classic", "UID, ATQA and SAK only (see Limits)."],
    ], [1.6 * inch, 3.15 * inch]),
])
s.append(PageBreak())

s += app_section("Emulate", ["11_emulate"], [
    P("Turns the watch into an NFC tag. Phones that tap it see a link, text or Wi-Fi login, as if it were a sticker."),
    *B(["Choose what to share, and type it in (or arrive here from Scan's <b>Emulate this tag</b> with it filled in).",
        "Choose the <b>Technology</b>: <b>NFC-A Type 4</b> works with iPhone and Android; <b>NFC-F Type 3</b> "
        "(FeliCa) is read well by Android and by phones sold in Japan; <b>Mixed</b> offers both.",
        "Tap <b>Start emulating</b> and hold the phone to the watch. The status shows when a reader connects and "
        "counts the reads. Tap <b>Stop</b> when done."]),
    P("LilyGO marks the Type 4 emulation in its library as experimental. If a phone won't read it, try Mixed or Type 3.", small),
])
s += app_section("Field detect", ["12_field"], [
    P("The watch's own radio stays off and listens. When another device sends an NFC field, the circle turns "
      "green, the watch vibrates, and the counters go up."),
    *B(["Find out whether a door, turnstile or payment terminal has an active NFC reader, and where it is.",
        "Check that a phone's NFC is actually switched on.",
        "See how often a reader polls (each new field counts as one event)."]),
])
s.append(PageBreak())

s += app_section("RF Lab", ["13_rflab", "13b_rflab_scrolled"], [
    P("A view into the NFC chip itself, useful when something isn't reading and you want to know why."),
    table([
        ["Reading", "What it means"],
        ["Chip / revision", "Confirms the ST25R3916 is powered and responding. If this is blank, the NFC chip isn't working."],
        ["VDD, VDD_RF, VDD_A, VDD_D, VDD_AM", "The chip's internal supply rails in millivolts. Around 3.3 V for VDD and VDD_RF is healthy; a low reading usually means a low battery."],
        ["External field", "YES when another reader's field is present (same as Field detect)."],
        ["Cap sensor", "The chip's built-in capacitance measurement (ST25R3916 only, not the 3916B)."],
        ["Amplitude / phase", "How strong the 13.56 MHz signal at the antenna is, and its phase. Shown live once the carrier is on."],
    ], [1.6 * inch, 3.15 * inch]),
    Spacer(1, 6),
    P("<b>Finding tags by detuning</b>: switch on <b>RF carrier</b>, keep the watch away from metal, and tap "
      "<b>Set baseline</b>. Now bring a tag, a phone or a hand close: amplitude and phase shift and the meter "
      "reports how much. This is the same effect the chip uses for its low-power wake-up mode."),
    P("If another reader is already transmitting, the chip won't switch its carrier on (collision avoidance); "
      "RF Lab tells you when that happens.", small),
])
s += app_section("History", ["14_history"], [
    P("A list of every tag scanned or dumped, newest first: time, chip type, UID and a one-line summary of the "
      "contents. Tap <b>Clear history</b> twice to delete it. The file lives at "
      "<font face='Mono'>/nfc/history.tsv</font>, so you can also open it on a computer."),
    P("Times come from the watch's real-time clock. If the clock has never been set, entries show time since power-on instead.", small),
])
s.append(PageBreak())

# ------------------------------------------------------------------ compatibility
s.append(P("What it can scan, write and emulate", h1))
s.append(P("The ST25R3916 speaks every common 13.56 MHz standard: NFC-A (ISO 14443A), NFC-B (ISO 14443B), "
           "NFC-F (FeliCa), NFC-V (ISO 15693) and ST25TB. This table shows what the app can do with the tags you're "
           "most likely to meet."))
Y, N, L = "Yes", "No", "Limited"
s.append(table([
    ["Tag / card", "Examples", "Scan", "Read data", "Write", "Dump", "Copy"],
    ["NTAG210/212/213/215/216", "Stickers, key fobs, business cards, NFC implants", Y, Y, Y, "Full + signature", Y],
    ["MIFARE Ultralight / EV1", "Disposable transit and event tickets", Y, Y, Y, Y, Y],
    ["MIFARE Classic 1K / 4K / Mini", "Older hotel keys, gym and office badges", "UID only", N, N, "UID only", N],
    ["MIFARE DESFire, NTAG 424, ST25TA (Type 4)", "Newer access cards, secure tags", Y, "If formatted", "If unlocked", "NFC file", Y],
    ["ICODE SLIX/SLIX2/DNA, ST25DV/TV, Tag-it (ISO 15693)", "Library books, ski passes, industrial tags", Y, Y, Y, "Full", Y],
    ["FeliCa Lite / Lite-S", "FeliCa NFC stickers", Y, Y, Y, "Open blocks", Y],
    ["FeliCa standard", "Suica, Octopus and similar transit cards", "IDm only", N, N, "IDm only", N],
    ["ST25TB / SRI512 / SRIX4K", "Some transit and laundry cards", Y, N, N, "Full", N],
    ["Bank / payment cards (EMV)", "Contactless debit and credit cards", Y, "App names", N, "App names only", N],
    ["Phones", "Android / iPhone", Y, "Via Emulate", "-", "-", "-"],
], [1.6 * inch, 1.35 * inch, 0.78 * inch, 0.82 * inch, 0.78 * inch, 0.86 * inch, 0.48 * inch]))
s.append(Spacer(1, 6))
s.append(P("\"Read data\" means the standard NFC content (NDEF): links, text, Wi-Fi and so on. A tag can also be "
           "locked by its owner; locked tags can be scanned and dumped but not written.", small))

s.append(P("What the watch can pretend to be (Emulate)", h2))
s.append(table([
    ["Emulated as", "Content", "Readable by"],
    ["NFC-A Type 4 tag", "Link, text or Wi-Fi", "iPhone XS and newer (hold the top edge to the watch) and Android"],
    ["NFC-F Type 3 tag (FeliCa)", "Link, text or Wi-Fi", "Android; phones with FeliCa"],
    ["Mixed A + F", "Link, text or Wi-Fi", "Whichever the phone polls first"],
], [1.75 * inch, 1.5 * inch, 3.55 * inch]))
s.append(P("Emulate shares information like a tag would. It can't impersonate a specific card's ID, so it won't "
           "open doors or work as a payment or transit card.", small))

s.append(P("Limits, and why", h2))
s += B(["<b>MIFARE Classic contents</b>: the sectors are encrypted with NXP's proprietary Crypto1 cipher. The "
        "ST25R3916 has no hardware for it, so only the card's ID, ATQA and SAK are shown.",
        "<b>Serial numbers (UIDs) can't be copied</b>: they're burned into each chip. Copy moves the stored contents only.",
        "<b>No permanent lock</b>: making a tag read-only can never be undone, so the app leaves it out to "
        "prevent accidents.",
        "<b>Password-protected pages</b> on NTAG/Ultralight stop the dump at the first protected page; the "
        "dump says where it stopped.",
        "<b>Payment cards</b>: only the public list of payment apps is read. Card numbers and transactions are never requested."])

# ------------------------------------------------------------------ install + troubleshooting
s.append(P("Installing the app", h1))
s.append(P("Option A: flash the ready-made firmware", h2))
s.append(P("Connect the watch by USB-C. On a computer with Python installed:"))
s.append(P("pip install esptool<br/>esptool --chip esp32s3 --port COM5 write-flash 0x0 "
           "NFC_Toolkit_TWatchUltra_0x0.bin", code))
s.append(P("Use your own port name (Windows: COMx; Mac: /dev/cu.usbmodem...; Linux: /dev/ttyACM0). If the port "
           "doesn't appear, hold <b>BOOT</b>, tap the reset/power button, release BOOT and try again. Browser "
           "flashers such as Espressif's ESP Tool page (Chrome or Edge) work too: add the file at address "
           "<b>0x0</b> and press Program."))
s.append(P("Option B: build from source", h2))
s += B(["Arduino IDE 2: install <b>esp32 by Espressif</b> 3.3.0 or newer from Boards Manager.",
        "Install <b>LilyGoLib</b> and copy everything from <b>LilyGoLib-ThirdParty</b> into your Arduino libraries folder.",
        "Open <font face='Mono'>NFC_Toolkit.ino</font>, choose board <b>LilyGo T-Watch-Ultra</b>, USB CDC On Boot "
        "<b>Enabled</b>, keep the default 16 MB partition scheme, and click Upload."])

s.append(P("Troubleshooting", h2))
s.append(table([
    ["Problem", "Try this"],
    ["Nothing happens when I hold a tag", "Move the tag slowly around the watch face and back; range is 1–3 cm. "
                                          "Check RF Lab: if the chip line is blank, the NFC chip isn't responding, so restart the watch."],
    ["\"Tag lost\" or a write times out", "Keep the tag still until the result appears. Metal surfaces under a sticker block NFC; use anti-metal tags."],
    ["Write fails: \"read-only\"", "The tag has been permanently locked. Use another tag."],
    ["Write fails: \"does not fit\"", "Shorten the content or use a bigger tag (NTAG216 holds 888 bytes)."],
    ["iPhone won't read Emulate", "Hold the top edge of the iPhone to the watch, screen on and unlocked. If it still won't read, try Mixed."],
    ["RF Lab won't turn the carrier on", "Another reader (or your phone) is transmitting nearby. Move away from it."],
    ["History says \"none\" for storage", "Insert a FAT32 microSD card, or restart so the app can prepare internal storage."],
], [2.1 * inch, 4.7 * inch]))
s.append(Spacer(1, 8))
s.append(P("Want detailed logs? Connect to a computer and open a serial monitor at 115200 baud: the NFC library "
           "prints each step there.", small))

doc = SimpleDocTemplate(os.path.join(REPO, "docs", "NFC_Toolkit_User_Guide.pdf"), pagesize=letter,
                        leftMargin=0.8 * inch, rightMargin=0.8 * inch, topMargin=0.75 * inch, bottomMargin=0.8 * inch,
                        title="NFC Toolkit for LilyGO T-Watch Ultra: User guide", author="NFC Toolkit")
doc.build(s, onFirstPage=on_page, onLaterPages=on_page)
print("ok")
