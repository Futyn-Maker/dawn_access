"""Cuts a test fixture out of a ground record: the cells one grid of it reached from a start, inside a
box of the world, as T lines (a cell's floor and its eight recorded steps, as features/GroundFile reads
them), with the grid's anchor, the character's position and any extra lines given.

python -I tools/ground/cut_ground.py [--until N] <record.tsv> <anchor index> <from x> <from y> <x0> <y0> <x1> <y1> <out.tsv> [extra line ...]

--until N reads the record only up to its line N: the ground as the mod knew it then.
"""

import math
import sys
from collections import deque

until = None
if len(sys.argv) > 2 and sys.argv[1] == "--until":
    until = int(sys.argv[2])
    del sys.argv[1:3]
path, want = sys.argv[1], int(sys.argv[2])
fx, fy = float(sys.argv[3]), float(sys.argv[4])
x0, y0, x1, y1 = map(float, sys.argv[5:9])
out_path = sys.argv[9]
extra = sys.argv[10:]

anchor_line = None
anchor = None
index = -1
steps = {}
floors = {}
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
    if index != want:
        continue
    if p[0] == "F":
        x, y, floor = int(p[1]), int(p[2]), float(p[4])
        floors.setdefault((x, y, int(math.floor(floor / anchor[4]))), floor)
    elif p[0] == "S":
        ax, ay, al, bx, by = map(int, p[1:6])
        # The step's last answer stands: one found free and asked again refused is refused.
        if p[6] != "X":
            steps[(ax, ay, al, bx, by)] = int(p[6])
        else:
            steps.pop((ax, ay, al, bx, by), None)

ox, oy, cell = anchor[0], anchor[1], anchor[3]


def column(x, y):
    return int(math.floor((x - ox) / cell)), int(math.floor((y - oy) / cell))


sx, sy = column(fx, fy)
start = min((c for c in floors if c[0] == sx and c[1] == sy), key=lambda c: c[2])
cx0, cy0 = column(x0, y0)
cx1, cy1 = column(x1, y1)
reached = {start}
queue = deque([start])
while queue:
    a = queue.popleft()
    for dy in (-1, 0, 1):
        for dx in (-1, 0, 1):
            if dx == 0 and dy == 0:
                continue
            land = steps.get((a[0], a[1], a[2], a[0] + dx, a[1] + dy))
            if land is None:
                continue
            b = (a[0] + dx, a[1] + dy, land)
            if not (cx0 <= b[0] <= cx1 and cy0 <= b[1] <= cy1) or b in reached:
                continue
            reached.add(b)
            queue.append(b)
lines = ["ground 1", anchor_line, "P %.2f %.2f %.2f" % (fx, fy, anchor[2])]
for c in sorted(reached):
    marks = ""
    for dy in (-1, 0, 1):
        for dx in (-1, 0, 1):
            if dx == 0 and dy == 0:
                continue
            land = steps.get((c[0], c[1], c[2], c[0] + dx, c[1] + dy))
            # A step out of the box is cut: the fixture holds only the box's ground.
            inside = cx0 <= c[0] + dx <= cx1 and cy0 <= c[1] + dy <= cy1
            if land is None or not inside:
                marks += "."
            else:
                marks += "-" if land < c[2] else ("+" if land > c[2] else "0")
    lines.append(
        "T %d %d %d %.1f %s" % (c[0], c[1], c[2], floors.get(c, float("nan")), marks)
    )
lines += extra
open(out_path, "w", encoding="utf-8", newline="\n").write("\n".join(lines) + "\n")
print("cells", len(reached), "lines", len(lines), "->", out_path)
