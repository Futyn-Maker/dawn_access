"""Cuts a test fixture out of a ground record around a change of the world's levels: the ground as one
grid of it was felt out before the change (T lines, as cut_ground.py cuts them), then, for the steps
whose answer the change turned from a wall to free, the wall as it was (an S line with the level it is
taken to lie in), the change and the free answers after it (S lines), and the changes after, as given.

python -I tools/ground/cut_world_change.py <record.tsv> <anchor index> <until line> <after line> <from x> <from y>
    <x0> <y0> <x1> <y1> <wall level> <out.tsv> <changes.txt>

<until line>: the record's line the change came at (answers before it are the world before);
<after line>: the line the world came back at (free answers between the two are the ones taken);
<changes.txt>: lines of "L in|out name ..." for the changes, the first one put before the free answers
and the rest after them.
"""

import math
import sys
from collections import deque

if len(sys.argv) > 1 and sys.argv[1] in ("-h", "--help"):
    print(__doc__)
    sys.exit(0)
if len(sys.argv) < 14:
    sys.exit(__doc__)
(
    path,
    want,
    until,
    after,
    fx,
    fy,
    x0,
    y0,
    x1,
    y1,
    wall_level,
    out_path,
    changes_path,
) = sys.argv[1:14]
want, until, after = int(want), int(until), int(after)
fx, fy, x0, y0, x1, y1 = map(float, (fx, fy, x0, y0, x1, y1))

anchor_line = None
anchor = None
index = -1
before = {}  # step -> (land or None, why), the last answer before the change
later = {}  # step -> land, the first free answer between the change and the world's return
floors = {}
for n, line in enumerate(open(path, encoding="utf-8"), start=1):
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
    if p[0] == "F" and n < until:
        x, y, floor = int(p[1]), int(p[2]), float(p[4])
        floors.setdefault((x, y, int(math.floor(floor / anchor[4]))), floor)
    elif p[0] == "S":
        key = tuple(map(int, p[1:6]))
        land = None if p[6] == "X" else int(p[6])
        if n < until:
            before[key] = (land, p[7])
        elif n < after and land is not None and key not in later:
            later[key] = land

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
            answer = before.get((a[0], a[1], a[2], a[0] + dx, a[1] + dy))
            if not answer or answer[0] is None:
                continue
            b = (a[0] + dx, a[1] + dy, answer[0])
            if (
                not (cx0 <= b[0] <= cx1 and cy0 <= b[1] <= cy1)
                or b in reached
                or b not in floors
            ):
                continue
            reached.add(b)
            queue.append(b)

turned = sorted(
    k
    for k, (land, why) in before.items()
    if land is None and why == "W" and k in later and (k[0], k[1], k[2]) in reached
)
changes = [l.strip() for l in open(changes_path, encoding="utf-8") if l.strip()]

with open(out_path, "w", encoding="utf-8", newline="\n") as out:
    out.write("ground 1\n")
    out.write(anchor_line + "\n")
    out.write("P %.2f %.2f %.2f\n" % (fx, fy, anchor[2]))
    for c in sorted(reached):
        marks = []
        for dy in (-1, 0, 1):
            for dx in (-1, 0, 1):
                if dx == 0 and dy == 0:
                    continue
                answer = before.get((c[0], c[1], c[2], c[0] + dx, c[1] + dy))
                if not answer or answer[0] is None:
                    marks.append(".")
                else:
                    marks.append(
                        "-" if answer[0] < c[2] else "+" if answer[0] > c[2] else "0"
                    )
        out.write(
            "T %d %d %d %.1f %s\n" % (c[0], c[1], c[2], floors[c], "".join(marks))
        )
    for k in turned:
        out.write("S %d %d %d %d %d X W 0.00 %s\n" % (k + (wall_level,)))
    if changes:
        out.write(changes[0] + "\n")
    for k in turned:
        out.write("S %d %d %d %d %d %d - 0.00\n" % (k + (later[k],)))
    for line in changes[1:]:
        out.write(line + "\n")
print("cells", len(reached), "walls turned free", len(turned))
