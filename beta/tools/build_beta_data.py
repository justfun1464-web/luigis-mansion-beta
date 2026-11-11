# Luigi's Mansion BETA: builds the edition's data files from the player's own disc.
#
#   python beta/tools/build_beta_data.py <extracted disc "files" folder> <output folder>
#
# The output folder becomes <program folder>/beta/files: the port serves every file there in place
# of the disc file with the same path (src/recomp/beta_files.cpp), and new files (events 102-106)
# as if they were on the disc. Everything here is derived from the player's disc at build time;
# nothing from the game is stored in this repository: only the edits (this script) and the BETA
# edition's own English text.
#
# What it changes (docs: beta/README.md):
#   - the Parlor scene (event 1) as in the E3 2001 build: Luigi's scream, the ghost's "... Ugh!",
#     the trio's "BOOOO!!!" and the professor's original story (Dr. Elvin Gad, who took the
#     mansion for an empty house), from the Japanese script's own lines
#   - the lab (event 8) restored from its older version left on the disc (event 26): the
#     "Oh, and... by the way..." beat, lines closer to the Japanese original
#   - "E. Gadd" -> "Elvin Gad" in every event text; Toads speak with the unused green Toad icon;
#     Mario's cry in the well uses the unused Mario icon
#   - ghost voices with the unused ghost icon: interference in the Game Boy Horror calls, the
#     blackout, the last call, a chain of whispers through the mansion
#   - myths: the Safari Hunter (Safari Room), a passage under the well into the unused statue
#     halls (maps 5 and 7) and the lantern hall (map 8), where Dr. Gad waits
#   - E3-style room name cards
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from lmtools import Archive, Jmp, decompress, yay0_compress  # noqa: E402

# Game flags used by the BETA events (the game has 256; retail uses up to 198 and 255).
F_SAFARI = 232      # Safari Hunter seen
F_WELL = 233        # passage under the well taken
F_HALLS = 234       # passed the first statue hall (cleared on the way back)
F_LANTERN = 235     # met Dr. Gad in the lantern hall
F_WHISPER = [236, 237, 238, 239, 240, 241]  # whispers, in order

ICON_NONE, ICON_GAD, ICON_GHOST, ICON_LUIGI, ICON_BOO, ICON_BOWSER, ICON_MARIO, ICON_TOAD_GREEN, ICON_TOAD_RED = 0, 1, 2, 3, 4, 5, 7, 16, 17


# ---------------------------------------------------------------------------------------------
class Event:
    """An event archive: script text (Shift-JIS, CRLF) and message table (entries split by CRLF)."""

    def __init__(self, files, n):
        self.n = n
        self.path = "Event/event%02d.szp" % n
        self.arc = Archive.parse(decompress(open(os.path.join(files, self.path), "rb").read()))
        text = [p for p, _ in self.arc.files() if p.startswith("text/")]
        self.script_path = text[0] if text else None
        msg = [p for p, _ in self.arc.files() if p.startswith("message/")]
        self.msg_path = msg[0] if msg else None
        self.script = self.arc.get(self.script_path).decode("shift_jis", errors="replace") if text else ""
        self.msgs = []
        if self.msg_path:
            data = self.arc.get(self.msg_path).decode("shift_jis", errors="replace")
            self.msgs = data.split("\r\n")
            if data.endswith("\r\n"):
                self.msgs.pop()
        self.changed = False

    # -- messages
    def set(self, i, text):
        while len(self.msgs) <= i:
            self.msgs.append("")
        self.msgs[i] = text
        self.changed = True

    def add(self, text):
        # after the table's own trailing blanks, so retail indices never move
        self.msgs.append(text)
        self.changed = True
        return len(self.msgs) - 1

    def replace_all(self, old, new):
        n = 0
        for i, m in enumerate(self.msgs):
            if old in m:
                self.msgs[i] = m.replace(old, new)
                n += 1
        if n:
            self.changed = True
        return n

    # -- script
    def sub(self, old, new, count=1):
        old = old.replace("\n", "\r\n")
        new = new.replace("\n", "\r\n")
        found = self.script.count(old)
        if found != count:
            raise SystemExit("event%02d: expected %d x %r, found %d" % (self.n, count, old[:60], found))
        self.script = self.script.replace(old, new)
        self.changed = True

    def sub_re(self, pattern, repl):
        s, n = re.subn(pattern, repl, self.script)
        if n:
            self.script = s
            self.changed = True
        return n

    def save(self, out):
        if not self.script_path:
            raise SystemExit("event%02d has no script" % self.n)
        self.arc.put(self.script_path, self.script.encode("shift_jis"))
        if self.msg_path:
            self.arc.put(self.msg_path, ("\r\n".join(self.msgs) + "\r\n").encode("shift_jis"))
        write(out, self.path, yay0_compress(self.arc.build()))


def new_event(files, template, n, script, msgs):
    """A new event archive (number n), shaped like `template`'s."""
    ev = Event(files, template)
    ev.n = n
    ev.path = "Event/event%d.szp" % n if n >= 100 else "Event/event%02d.szp" % n
    ev.script = script.replace("\r\n", "\n").replace("\n", "\r\n")
    if not ev.msg_path:
        raise SystemExit("template event %d has no message table" % template)
    ev.msgs = list(msgs)
    ev.changed = True
    return ev


def write(out, rel, data):
    p = os.path.join(out, *rel.split("/"))
    os.makedirs(os.path.dirname(p), exist_ok=True)
    with open(p, "wb") as f:
        f.write(data)
    print("  wrote %s (%d bytes)" % (rel, len(data)))


def say(icon, idx, window=0):
    return "<WINDOW>(%d)<COLOR>(%d)<SPEAK>(%d)" % (window, icon, idx)


# ---------------------------------------------------------------------------------------------
# Event 1: the Parlor (E3 2001)
def parlor(files, out):
    ev = Event(files, 1)
    # Text: the Japanese script's own story (the professor took the mansion for an empty house),
    # the cut voices kept in the US table (0, 1) and a "BOOOO!!!" for the trio.
    ev.set(0, "Aaahhhh!")
    ev.set(1, "... Ugh!")
    ev.set(2, "Ouch, ouch, ouch...")
    ev.set(3, "Well now, what's all this?\nBarging into a man's house\nwithout so much as a knock...")
    ev.set(4, "Eh? YOU own this mansion?!\nYour name is Luigi? Well,\nI never knew...")
    ev.set(5, "...Me? I study ghosts.\nThe name's <COLOR>(3)Dr. Elvin Gad<COLOR>(0).")
    ev.set(6, "I took the place for an empty\nhouse, so I've been living here\nfor ages. Sorry, sorry!")
    ev.set(7, "This house is famous for its\nghosts, you see. Perfect for\nmy research. Do forgive me.")
    ev.set(9, "Whoa! No time to dawdle!")
    ev.set(10, "<COLOR>(3)Luigi<COLOR>(0), my boy! Follow me,\nquick as you can!!")
    boo = ev.add("BOOOO!!!")
    # Luigi screams when the ghost shows itself; the window the timeline closes 82 frames later
    # (the cut line's own <CLOSEWINDOW>) closes it.
    ev.sub('<ACTOR>"demo_obake"<ALPHA>(180)\n', '<ACTOR>"demo_obake"<ALPHA>(180)\n%s\n' % say(ICON_LUIGI, 0))
    # The ghost's "... Ugh!" as the professor's vacuum catches it, held through the next sound.
    ev.sub('<ACTOR>"demo_obake"<BEAM>\n', '<ACTOR>"demo_obake"<BEAM>\n%s\n' % say(ICON_GHOST, 1))
    ev.sub("<WAIT>(39)//timing\n<CLOSEWINDOW>(0)\n<SE>(1)\n\n<WAIT>(37)//timing\n<SE>(2)",
           "<WAIT>(39)//timing\n<SE>(1)\n\n<WAIT>(37)//timing\n<CLOSEWINDOW>(0)\n<SE>(2)")
    # The trio's "BOOOO!!!" as they fade in (closed by the timeline's next <CLOSEWINDOW>).
    ev.sub('<ACTOR>"demo_obake3"<COLOFF>\n<FRAMEEND>\n',
           '<ACTOR>"demo_obake3"<COLOFF>\n<FRAMEEND>\n%s\n' % say(ICON_GHOST, boo))
    return ev


# Event 8: the lab, from its older version (event 26)
def lab(files, out):
    ev = Event(files, 8)
    # event 26's beat after the Boo explanation: "Oh, and... by the way...", then the usual menu
    ev.sub("<WINDOW>(0)<COLOR>(1)<SPEAK>(30)\n<SE>(201)\n<ANYKEY>\n",
           "<WINDOW>(0)<COLOR>(1)<SPEAK>(30)\n<SE>(201)\n<ANYKEY>\n<WINDOW>(0)<COLOR>(1)<SPEAK>(31)\n<SE>(132)\n<WAIT>(20)\n<ANYKEY>\n")
    ev.set(15, "This is where I study ghosts,\nday and night. Also known as\n<COLOR>(3)Dr. Elvin Gad's<COLOR>(0) Ghost\nResearch Laboratory!\n...The Lab, for short.")
    ev.set(17, "Tell me, does that look like\na REAL mansion to you?")
    ev.set(23, "Now, those Boos won't EAT him.\nBut what became of him...?\nSome who are taken never\ncome back, you know.")
    ev.set(31, "Oh, and...by the way...")
    ev.set(36, "Anyone who can't fight ghosts\nis easy prey for them. Hurry,\ngo and save your brother!")
    ev.set(37, "...Very well, I'll lend a hand.\nI'll teach you how to deal\nwith ghosts. Follow me,\n<COLOR>(3)Luigi<COLOR>(0)!")
    return ev


def ghost_voices(events):
    # Interference in the professor's calls (events 11, 42, 80: same script).
    for n in (11, 42, 80):
        ev = events[n]
        i = ev.add("KRZZZ...he...can't...save...\nyou...KRZZZ")
        ev.sub("<WINDOW>(2)<COLOR>(1)<SPEAK>(1)\n<SE>(134)\n<ANYKEY>\n",
               "<WINDOW>(2)<COLOR>(1)<SPEAK>(1)\n<SE>(134)\n<ANYKEY>\n%s\n<WAIT>(70)\n<ANYKEY>\n" % say(ICON_GHOST, i, 2))
    # The blackout: someone giggles on the line first.
    ev = events[44]
    i = ev.add("Hee hee hee...\nLights out, little Luigi...")
    ev.sub('<PDEMO>"gb_call01"\n<WINDOW>(2)<COLOR>(1)<SPEAK>(0)\n',
           '<PDEMO>"gb_call01"\n%s\n<WAIT>(80)\n<ANYKEY>\n<WINDOW>(2)<COLOR>(1)<SPEAK>(0)\n' % say(ICON_GHOST, i, 2))
    # The last call: the mansion is not done with Luigi.
    ev = events[88]
    i = ev.add("KRZZZ...the mansion...\nremembers...you...KRZZZ")
    ev.sub("<WINDOW>(2)<COLOR>(1)<SPEAK>(12)\n<SE>(118)\n<ANYKEY>\n",
           "<WINDOW>(2)<COLOR>(1)<SPEAK>(12)\n<SE>(118)\n<ANYKEY>\n%s\n<WAIT>(60)\n<ANYKEY>\n" % say(ICON_GHOST, i, 2))
    # Mario's cry in the well: the unused Mario icon.
    events[46].sub("<WINDOW>(0)<COLOR>(0)<SPEAK>(0)", say(ICON_MARIO, 0), count=1)


# ---------------------------------------------------------------------------------------------
# New events (102-106): the event tables have 109 slots (stub code for 102-108).
WHISPERS = [
    "...it's so cold in here...",
    "Turn around, Luigi...",
    "We were guests here too, once.",
    "Shhh... the walls are listening.",
    "Your brother screamed\njust like that.",
    "Nobody leaves this house.\nNot really.",
]


def whisper_script():
    lines = ["<MENUOFF>", "<WAIT>(25)"]
    # each trigger needs the previous whisper's flag: the next one in the chain is always the one
    # that fired, so its own flag is the first one still off
    for k, flag in enumerate(F_WHISPER):
        lab_ = "w%d" % k
        lines += ['<CHECKFLAG>(%d)"%s""%s_go"' % (flag, "w%d" % (k + 1) if k + 1 < len(F_WHISPER) else "done", lab_),
                  '<CASE>"%s_go"' % lab_,
                  "<FLAGON>(%d)" % flag,
                  say(ICON_GHOST, k),
                  "<WAIT>(110)",
                  "<CLOSEWINDOW>(0)",
                  "<MENUON>",
                  "<END>",
                  '<CASE>"%s"' % ("w%d" % (k + 1) if k + 1 < len(F_WHISPER) else "done")]
    lines += ["<MENUON>", "<END>"]
    return "\n".join(lines) + "\n"


SAFARI = ["...Hold still...",
          "My, what a fine specimen.\nYou'd look lovely mounted\nabove my fireplace.",
          "......!!",
          "Every head on these walls\nonce walked these halls,\nyou know.",
          "Run along, little green one.\nThe hunt begins at midnight..."]

SAFARI_SCRIPT = """<MENUOFF>
<LUIGISTOP>
<WAIT>(30)
<CAMSHAKE>
<WAIT>(25)
%s
<ANYKEY>
%s
<ANYKEY>
<CAMSHAKE>
%s
<ANYKEY>
%s
<ANYKEY>
%s
<ANYKEY>
<CLOSEWINDOW>(0)
<FLAGON>(%d)
<LUIGIFREE>
<MENUON>
<END>
""" % (say(ICON_GHOST, 0), say(ICON_GHOST, 1), say(ICON_LUIGI, 2), say(ICON_GHOST, 3), say(ICON_GHOST, 4), F_SAFARI)

WELL = ["Lu...i...gi...", "Down here...\ndeeper...", "......?"]
WELL_SCRIPT = """<MENUOFF>
<LUIGISTOP>
<WAIT>(20)
%s
<WAIT>(80)
%s
<WAIT>(90)
%s
<ANYKEY>
<CLOSEWINDOW>(0)
<FLAGON>(%d)
<BGMSTOP>
<WIPEKIND>(1)
<WARP>(5)
<END>
""" % (say(ICON_GHOST, 0), say(ICON_GHOST, 1), say(ICON_LUIGI, 2), F_WELL)

HALLS = ["...deeper...", "Nobody was ever meant\nto come down here."]
HALLS_SCRIPT = """<MENUOFF>
<LUIGISTOP>
<WAIT>(10)
<CHECKFLAG>(%d)"second""first"
<CASE>"first"
<FLAGON>(%d)
%s
<WAIT>(90)
<CLOSEWINDOW>(0)
<WIPEKIND>(1)
<WARP>(7)
<END>
<CASE>"second"
%s
<WAIT>(110)
<CLOSEWINDOW>(0)
<WIPEKIND>(1)
<WARP>(8)
<END>
""" % (F_HALLS, F_HALLS, say(ICON_GHOST, 0), say(ICON_GHOST, 1))

LANTERN = ["...<COLOR>(3)Luigi<COLOR>(0)? Is that you?\nHow in the world did you\nget all the way down here?",
           "These halls were never\nfinished. I sealed them off\na long, long time ago...",
           "The ghosts I studied down here\nweren't the kind you can put\nback in a painting.",
           "Go back up, my boy. And\nwhatever you heard down here...\nforget it."]
LANTERN_SCRIPT = """<MENUOFF>
<LUIGISTOP>
<WAIT>(20)
%s
<ANYKEY>
<SPEAK>(1)
<ANYKEY>
<SPEAK>(2)
<ANYKEY>
<SPEAK>(3)
<ANYKEY>
<CLOSEWINDOW>(0)
<FLAGOFF>(%d)
<FLAGON>(%d)
<SETLUIGI>(0)
<WIPEKIND>(3)
<BGM>(26)
<WARP>(2)
<WAIT>(10)
<END>
""" % (say(ICON_GAD, 0), F_HALLS, F_LANTERN)


# ---------------------------------------------------------------------------------------------
# Map tables
def event_row(t, template_row, pos, no, area, eventif, flag=0, disappear=0, stop=1, lock=1, load=0):
    r = t.add_row(template_row)
    t.set(r, "name", "event")
    if t.field_index("CharacterName") >= 0:
        t.set(r, "CharacterName", "(null)")
    for k, v in zip(("pos_x", "pos_y", "pos_z"), pos):
        t.set(r, k, v)
    for k in ("dir_x", "dir_y", "dir_z"):
        t.set(r, k, 0.0)
    for k in ("scale_x", "scale_y", "scale_z"):
        t.set(r, k, 1.0)
    vals = {"EventNo": no, "EventArea": area, "EventFlag": flag, "EventTime": 0, "EventLoad": load,
            "disappear_flag": disappear, "event_parameter": 0, "EventIf": eventif, "EventLock": lock, "PlayerStop": stop}
    for k, v in vals.items():
        if t.field_index(k) >= 0:
            t.set(r, k, v)
    return r


def blank_row(t):
    t.rows.append(bytearray(t.entry_size))
    return len(t.rows) - 1


def mansion_map(files, out):
    path = "Map/map2.szp"
    arc = Archive.parse(decompress(open(os.path.join(files, path), "rb").read()))
    t = Jmp(arc.get("jmp/eventinfo"))
    tmpl = next(r for r in range(t.n_entries) if t.get(r, "EventNo") == 14) if any(
        t.get(r, "EventNo") == 14 for r in range(t.n_entries)) else 0
    # Safari Hunter: first time in the Safari Room (room 52, 3F east)
    event_row(t, tmpl, (3150.0, 1100.0, -200.0), 102, 450, 5, disappear=F_SAFARI)
    # The passage under the well: by the ladder at the bottom of the well (room 69), once Mario's
    # vision there (event 46, flag 53) has played
    event_row(t, tmpl, (680.0, -550.0, -5990.0), 105, 160, 5, flag=53, disappear=F_WELL)
    # Whispers, one room after the other (each needs the previous one's flag)
    spots = [((-370.0, 0.0, -1800.0), 9),      # Dining Room
             ((-2990.0, 0.0, -1790.0), 8),     # Kitchen
             ((2700.0, 0.0, -2070.0), 10),     # Ballroom
             ((-1800.0, 550.0, -2035.0), 34),  # Study
             ((-1770.0, 550.0, -3370.0), 38),  # Wardrobe Room
             ((0.0, 1100.0, -2170.0), 56)]     # Clockwork Room
    prev = 0
    for (pos, _room), flag in zip(spots, F_WHISPER):
        event_row(t, tmpl, pos, 106, 380, 5, flag=prev, disappear=flag, stop=0, lock=0, load=1)
        prev = flag
    arc.put("jmp/eventinfo", t.build())
    write(out, path, yay0_compress(arc.build()))


def statue_hall(files, out, n, z_end):
    path = "Map/map%d.szp" % n
    arc = Archive.parse(decompress(open(os.path.join(files, path), "rb").read()))
    t = Jmp(arc.get("jmp/eventinfo"))
    ref = Jmp(Archive.parse(decompress(open(os.path.join(files, "Map/map2.szp"), "rb").read())).get("jmp/eventinfo"))
    if [f[:5] for f in t.fields] != [f[:5] for f in ref.fields] or t.entry_size != ref.entry_size:
        raise SystemExit("map%d eventinfo layout differs from the mansion's" % n)
    r0 = blank_row(t)  # a row with the strings in place to copy from
    t.rows[r0] = bytearray(ref.rows[0])
    for x in (2150.0, 3050.0):  # end of the left and the right corridor
        event_row(t, r0, (x, 0.0, z_end), 103, 420, 5)
    del t.rows[r0]
    arc.put("jmp/eventinfo", t.build())
    write(out, path, yay0_compress(arc.build()))


def lantern_hall(files, out):
    # Map 8's tables predate the retail layout (the port rebuilds them in map 5's on load,
    # src/recomp/unused_maps.cpp); its one event row stands on Dr. Gad (lohakase).
    path = "Map/map8.szp"
    arc = Archive.parse(decompress(open(os.path.join(files, path), "rb").read()))
    t = Jmp(arc.get("jmp/eventinfo"))
    t.set(0, "EventNo", 104)
    t.set(0, "EventArea", 480)
    t.set(0, "EventIf", 5)
    t.set(0, "EventLock", 1)
    arc.put("jmp/eventinfo", t.build())
    write(out, path, yay0_compress(arc.build()))



# ---------------------------------------------------------------------------------------------
# Room name cards (kawano/roomname/e_*.bti in the resident game archive): E3-style literal names
# (the internal file names: living = living room, hakuse(i) = stuffed trophies, gensou = fantasy,
# akichi = empty lot, sofubo = grandparents...). Letters are cut from the retail cards (white
# serif with an outline, I4) and set again; "E" is the retail "F" with the "L"'s foot.
ROOM_SOURCE = {
    'e_akiti': "Boneyard", 'e_atelier': "The Artist's Studio", 'e_baby': "Nursery", 'e_balco': "Balcony",
    'e_bath': "Bathroom", 'e_bedg': "Guest Room", 'e_beds': "Master Bedroom", 'e_child': "The Twins' Room",
    'e_closed': "Sealed Room", 'e_closet': "Wardrobe Room", 'e_dance': "Ball Room", 'e_denwa': "Telephone Room",
    'e_dining': "Dining Room", 'e_entran': "Foyer", 'e_freez': "Cold Storage", 'e_galler': "Gallery",
    'e_game': "Billiards Room", 'e_garden': "Courtyard", 'e_gensou': "Observatory", 'e_grave': "Graveyard",
    'e_guest': "Sitting Room", 'e_haiden': "Breaker Room", 'e_haikan': "Pipe Room", 'e_hakuse': "Safari Room",
    'e_hikae': "Anteroom", 'e_ido': "Bottom of the Well", 'e_kaisou': "Astral Hall", 'e_karakuri': "Clockwork Room",
    'e_keep': "Cellar", 'e_kitche': "Kitchen", 'e_kunren': "Training Room", 'e_living': "Parlor",
    'e_mirror': "Mirror Room", 'e_music': "Conservatory", 'e_nando': "Storage Room", 'e_saidan': "Secret Altar",
    'e_secret': "Hidden Room", 'e_sentaku': "Laundry Room", 'e_sofubo': "Nana's Room", 'e_study': "Study",
    'e_tea': "Tea Room", 'e_toile': "Washroom", 'e_traini': "Rec Room", 'e_tubo': "Ceramics Studio",
    'e_tv': "Projection Room", 'e_uranai': "Fortune-teller's Room", 'e_wait': "Butler's Room", 'e_yoroi': "Armory",
    'e_roof': "Roof",
}
ROOM_NAMES = {
    'e_entran': "Entrance", 'e_living': "Living Room", 'e_hakuse': "Trophy Room", 'e_gensou': "Fantasy Room",
    'e_akiti': "Empty Lot", 'e_sofubo': "Grandparents' Room", 'e_baby': "Baby Room", 'e_child': "Children's Room",
    'e_beds': "Bedroom", 'e_bedg': "Guest Bedroom", 'e_guest': "Guest Room", 'e_music': "Music Room",
    'e_traini': "Gymnasium", 'e_game': "Game Room", 'e_tv': "Movie Room", 'e_nando': "Storeroom",
    'e_toile': "Toilet", 'e_sentaku': "Laundry", 'e_haiden': "Power Room", 'e_ido': "The Well",
    'e_saidan': "Altar", 'e_yoroi': "Armor Room", 'e_tubo': "Pot Room", 'e_karakuri': "Doll Room",
    'e_atelier': "Atelier", 'e_hikae': "Waiting Room", 'e_closet': "Closet", 'e_dance': "Dance Hall",
    'e_garden': "Garden", 'e_freez': "Cold Room", 'e_secret': "Secret Room", 'e_labo': "Laboratory",
}


def encode_i4(a):
    """HxW uint8 intensity -> GX I4 (8x8 tiles, two texels per byte, high nibble first)."""
    h, w = a.shape
    out = bytearray()
    for by in range(0, h, 8):
        for bx in range(0, w, 8):
            for y in range(8):
                for x in range(0, 8, 2):
                    p = [a[by + y, bx + x + k] if by + y < h and bx + x + k < w else 0 for k in (0, 1)]
                    out.append((int(p[0]) >> 4) << 4 | (int(p[1]) >> 4))
    return bytes(out)


def room_cards(game):
    import numpy as np
    from scipy import ndimage
    from PIL import ImageFont
    from lmtools import read_bti
    ref = None
    for path in ("/usr/share/fonts/truetype/dejavu/DejaVuSerif.ttf", "C:/Windows/Fonts/times.ttf"):
        if os.path.exists(path):
            ref = ImageFont.truetype(path, 40)
            break
    if ref is None:
        raise SystemExit("no reference serif font for the letter widths")
    lib = {}
    for key, text in ROOM_SOURCE.items():
        b = game.get("kawano/roomname/%s.bti" % key)
        if b is None:
            continue
        img = read_bti(b)[..., 3].astype(np.int32)
        ink = np.nonzero(img.sum(0))[0]
        if len(ink) == 0:
            continue
        words = []
        start = prev = ink[0]
        for x in ink[1:]:
            if x - prev >= 6:
                words.append((start, prev + 1))
                start = x
            prev = x
        words.append((start, prev + 1))
        toks = text.split(" ")
        if len(words) != len(toks):
            continue
        for (x0, x1), w in zip(words, toks):
            col = img[:, x0:x1].sum(0)
            bounds = [ref.getlength(w[:i]) for i in range(len(w) + 1)]
            cuts = [0]
            for i in range(1, len(w)):
                est = int(round(bounds[i] / bounds[-1] * (x1 - x0)))
                lo, hi = max(cuts[-1] + 2, est - 4), min(x1 - x0 - 2, est + 5)
                cuts.append(lo + int(np.argmin(col[lo:hi])) if lo < hi else est)
            cuts.append(x1 - x0)
            for i, ch in enumerate(w):
                g = img[:, x0 + cuts[i]:x0 + cuts[i + 1]].copy()
                # drop specks of the neighbours' serifs that the cut left at the edges
                lab, n = ndimage.label(g > 0)
                for k in range(1, n + 1):
                    if (lab == k).sum() < 8:
                        g[lab == k] = 0
                lib.setdefault(ch, []).append(g)

    def glyph(ch):
        if ch == "E" and "E" not in lib:
            f = pick("F").copy()
            l = pick("L")
            h = min(f.shape[1], l.shape[1])
            f[20:, :h] = np.maximum(f[20:, :h], l[20:, :h])
            return f
        return pick(ch)

    def pick(ch):
        c = sorted(lib[ch], key=lambda g: g.shape[1])
        return c[len(c) // 2]

    out = {}
    for key, text in ROOM_NAMES.items():
        pieces = []
        for wi, w in enumerate(text.split(" ")):
            if wi:
                pieces.append(np.zeros((32, 12), np.int32))
            for ch in w:
                pieces.append(glyph(ch))
        img = np.concatenate(pieces, 1)
        width = min(320, (img.shape[1] + 4 + 7) // 8 * 8)
        card = np.zeros((32, width), np.uint8)
        card[:, :min(width, img.shape[1])] = np.clip(img[:, :width], 0, 255)
        hdr = bytearray(game.get("kawano/roomname/%s.bti" % key)[:0x20])
        hdr[2:4] = width.to_bytes(2, "big")
        hdr[0x1C:0x20] = (0x20).to_bytes(4, "big")
        out[key] = bytes(hdr) + encode_i4(card)
    return out


def game_archive(files, out):
    path = "Game/game_usa.szp"
    arc = Archive.parse(decompress(open(os.path.join(files, path), "rb").read()))
    for key, bti in room_cards(arc).items():
        arc.put("kawano/roomname/%s.bti" % key, bti)
    write(out, path, yay0_compress(arc.build()))


# ---------------------------------------------------------------------------------------------
def main():
    if len(sys.argv) < 3:
        print(__doc__ or "usage: build_beta_data.py <disc files> <out>")
        raise SystemExit(2)
    files, out = sys.argv[1], sys.argv[2]
    os.makedirs(out, exist_ok=True)
    events = {}
    for name in sorted(os.listdir(os.path.join(files, "Event"))):
        m = re.match(r"event(\d+)\.szp$", name)
        if m:
            ev = Event(files, int(m.group(1)))
            if ev.script_path:
                events[ev.n] = ev
    events[1] = parlor(files, out)
    events[8] = lab(files, out)
    for ev in events.values():
        ev.replace_all("Professor E. Gadd", "Dr. Elvin Gad")
        ev.replace_all("E. Gadd", "Elvin Gad")
        ev.replace_all("Gadd", "Gad")
        if ev.sub_re(r"<COLOR>\(17\)<SPEAK>", "<COLOR>(16)<SPEAK>"):
            pass  # Toads: the green Toad's icon (the green Toad model is on in the BETA preset)
    ghost_voices(events)
    new = [new_event(files, 14, 102, SAFARI_SCRIPT, SAFARI),
           new_event(files, 14, 103, HALLS_SCRIPT, HALLS),
           new_event(files, 14, 104, LANTERN_SCRIPT, LANTERN),
           new_event(files, 14, 105, WELL_SCRIPT, WELL),
           new_event(files, 14, 106, whisper_script(), WHISPERS)]
    for ev in list(events.values()) + new:
        if ev.changed:
            ev.save(out)
    mansion_map(files, out)
    statue_hall(files, out, 5, -3400.0)
    statue_hall(files, out, 7, -5400.0)
    lantern_hall(files, out)
    game_archive(files, out)
    print("done")


if __name__ == "__main__":
    main()
