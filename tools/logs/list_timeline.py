"""The list's changes over a session log: for each 'explore: listed at' line, which targets are listed and
which are left out with why, printed only when the listed set or a reason changes.
python -I tools/logs/list_timeline.py <log> [from hh:mm:ss] [to hh:mm:ss]"""

import re
import sys

if len(sys.argv) > 1 and sys.argv[1] in ("-h", "--help"):
    print(__doc__)
    sys.exit(0)
if len(sys.argv) < 2:
    sys.exit(__doc__)
path = sys.argv[1]
start = sys.argv[2] if len(sys.argv) > 2 else "00:00:00"
end = sys.argv[3] if len(sys.argv) > 3 else "99:99:99"
line_re = re.compile(
    r"^\[(\d\d:\d\d:\d\d\.\d+)\] \[info \] explore: listed at \(([^)]*)\): \[(.*?)\](?:; left out: \[(.*)\])?$"
)
item_re = re.compile(
    r"(?:way |)(.+?) ((?:BatesCharacterTriggerVolume|BP_CharacterTriggerVolume_C|BP_BatesUseLocation_C)_?\d*) at \((-?\d+), (-?\d+), (-?\d+)\)(?:: (.*))?$"
)
names = {}


def split_items(s):
    out = []
    depth = 0
    cur = ""
    for ch in s:
        if ch == "(":
            depth += 1
        elif ch == ")":
            depth -= 1
        if ch == ";" and depth == 0:
            out.append(cur.strip())
            cur = ""
        else:
            cur += ch
    if cur.strip():
        out.append(cur.strip())
    return out


prev = None
for raw in open(path, encoding="utf-8", errors="replace"):
    m = line_re.match(raw.rstrip("\n"))
    if not m:
        continue
    t = m.group(1)
    if t < start or t > end:
        continue
    here = m.group(2)
    listed = []
    for it in split_items(m.group(3)):
        im = item_re.match(it)
        listed.append(
            (im.group(1).replace("way ", ""), im.group(2)) if im else (it, "")
        )
    out = []
    for it in split_items(m.group(4) or ""):
        im = item_re.match(it)
        if im:
            out.append(
                (im.group(1).replace("way ", ""), im.group(2), im.group(6) or "")
            )
    state = (tuple(listed), tuple((a, b, c) for a, b, c in out))
    if state == prev:
        continue
    prevl = set(prev[0]) if prev else set()
    prevo = {(a, b): c for a, b, c in prev[1]} if prev else {}
    now_l = set(listed)
    added = [x for x in listed if x not in prevl]
    # In the order the list held them before: a set's order changes from run to run.
    removed = [x for x in prev[0] if x not in now_l] if prev else []
    changed = [
        (a, b, c)
        for a, b, c in out
        if prevo.get((a, b)) not in (None, c) and (a, b) not in prevl
    ]
    print(
        "%s at (%s): +%s -%s%s"
        % (
            t,
            here,
            [("%s %s" % x) for x in added],
            [
                (
                    "%s %s: %s"
                    % (x[0], x[1], dict(((a, b), c) for a, b, c in out).get(x, "gone"))
                )
                for x in removed
            ],
            (" ~" + str([("%s %s: %s" % x) for x in changed])) if changed else "",
        )
    )
    prev = state
