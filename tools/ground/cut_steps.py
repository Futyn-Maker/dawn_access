"""Cuts a test fixture of step answers out of a ground record: every step one grid of it answered
last that starts or lands in the columns under a box of the world grown by a margin, as S lines
(free with its landing, or refused: X), with the record's header and the grid's anchor. Unlike
cut_ground.py, which keeps the free steps only (as T lines), the refusals stay: what the ground
kept refused is what a stretch asked again weighs its answers against (features/GroundRecheck).

python -I tools/ground/cut_steps.py [--until N] <record.tsv> <anchor index> <x0> <y0> <x1> <y1> <margin cm> <out.tsv>

--until N reads the record only up to its line N: the ground as the mod knew it then.
"""

import math
import sys

until = None
if len(sys.argv) > 2 and sys.argv[1] == "--until":
    until = int(sys.argv[2])
    del sys.argv[1:3]
path, want = sys.argv[1], int(sys.argv[2])
x0, y0, x1, y1, margin = map(float, sys.argv[3:8])
out_path = sys.argv[8]

anchor_line = None
anchor = None
index = -1
last = {}
for number, line in enumerate(open(path, encoding="utf-8"), 1):
    if until is not None and number > until:
        break
    p = line.split()
    if not p:
        continue
    if p[0] == "A":
        index += 1
        if index == want:
            anchor_line = line.strip()
            anchor = list(map(float, p[1:]))
        continue
    if index != want or p[0] != "S" or len(p) < 9:
        continue
    last[tuple(map(int, p[1:6]))] = line.strip()

if anchor is None:
    sys.exit(f"no anchor {want} in {path}")
ox, oy, cell = anchor[0], anchor[1], anchor[3]
cx0, cx1 = math.floor((x0 - margin - ox) / cell), math.floor((x1 + margin - ox) / cell)
cy0, cy1 = math.floor((y0 - margin - oy) / cell), math.floor((y1 + margin - oy) / cell)


def inside(x, y):
    return cx0 <= x <= cx1 and cy0 <= y <= cy1


kept = [last[k] for k in sorted(last) if inside(k[0], k[1]) or inside(k[3], k[4])]
with open(out_path, "w", encoding="utf-8", newline="\n") as out:
    out.write("ground 1\n")
    out.write(anchor_line + "\n")
    for line in kept:
        out.write(line + "\n")
print(f"{len(kept)} step(s) in columns x {cx0}..{cx1}, y {cy0}..{cy1} -> {out_path}")
