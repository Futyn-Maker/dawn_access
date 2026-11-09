"""Replays one grid of a ground record (the n-th anchor line, counted from 0) from the cell holding a
world point, over the recorded free steps, and draws the reached cells in a window of the world:
'#' reached, '.' a floor probed there but not reached, ' ' nothing known. Then lists the refused
steps out of the reached cells in the window, with why and how far the sweep got, by each cell's
middle (as Exploration places it, half a cell in from the grid's corner).

python -I tools/ground/ground_window.py <record.tsv> <anchor index> <from x> <from y> <x0> <y0> <x1> <y1>
"""

import math
import sys
from collections import deque

path = sys.argv[1]
want = int(sys.argv[2])
fx, fy = float(sys.argv[3]), float(sys.argv[4])
x0, y0, x1, y1 = map(float, sys.argv[5:9])

anchor = None
index = -1
steps = {}
probes = {}
for line in open(path, encoding="utf-8"):
    p = line.split()
    if not p:
        continue
    if p[0] == "A":
        index += 1
        if index == want:
            anchor = list(map(float, p[1:]))
        continue
    if index != want:
        continue
    if p[0] == "F":
        probes[(int(p[1]), int(p[2]), int(p[3]))] = float(p[4])
    elif p[0] == "S":
        ax, ay, al, bx, by = map(int, p[1:6])
        land, why = p[6], p[7] if len(p) > 7 else "-"
        clear = float(p[8]) if len(p) > 8 else 0.0
        steps[(ax, ay, al, bx, by)] = (None if land == "X" else int(land), why, clear)

ox, oy, oz, cell = anchor[:4]
level_size = anchor[4]
print("anchor", anchor[:3], "steps", len(steps), "probes", len(probes))


def column(x, y):
    return int(math.floor((x - ox) / cell)), int(math.floor((y - oy) / cell))


def middle(c):
    return ox + (c[0] + 0.5) * cell, oy + (c[1] + 0.5) * cell


# The start: the probed floor of the point's column that most steps leave from.
sx, sy = column(fx, fy)
candidates = [c for c in probes if c[0] == sx and c[1] == sy]
if not candidates:
    candidates = [min(probes, key=lambda c: math.hypot(c[0] - sx, c[1] - sy))]
start = max(
    candidates,
    key=lambda c: sum(1 for k, v in steps.items() if k[:3] == c and v[0] is not None),
)
reached = {start}
queue = deque([start])
while queue:
    a = queue.popleft()
    for dy in (-1, 0, 1):
        for dx in (-1, 0, 1):
            if dx == 0 and dy == 0:
                continue
            s = steps.get((a[0], a[1], a[2], a[0] + dx, a[1] + dy))
            if not s or s[0] is None:
                continue
            b = (a[0] + dx, a[1] + dy, s[0])
            if b not in reached:
                reached.add(b)
                queue.append(b)
print("start", start, "reached", len(reached))
cx0, cy0 = column(x0, y0)
cx1, cy1 = column(x1, y1)
flat = {}
for c in reached:
    flat.setdefault((c[0], c[1]), []).append(c[2])
probed = {(c[0], c[1]) for c in probes}
print(
    "columns x %d..%d (middles %.0f..%.0f), rows y %d..%d (middles %.0f..%.0f, top row first)"
    % (
        cx0,
        cx1,
        middle((cx0, 0))[0],
        middle((cx1, 0))[0],
        cy1,
        cy0,
        middle((0, cy1))[1],
        middle((0, cy0))[1],
    )
)
for y in range(cy1, cy0 - 1, -1):
    row = ""
    for x in range(cx0, cx1 + 1):
        row += "#" if (x, y) in flat else ("." if (x, y) in probed else " ")
    print("%6.0f %s" % (middle((0, y))[1], row))
print("refused steps out of reached cells in the window:")
for c in sorted(reached):
    if not (cx0 <= c[0] <= cx1 and cy0 <= c[1] <= cy1):
        continue
    outs = []
    for dy in (-1, 0, 1):
        for dx in (-1, 0, 1):
            s = steps.get((c[0], c[1], c[2], c[0] + dx, c[1] + dy))
            if s and s[0] is None:
                outs.append("%+d%+d:%s%.0f" % (dx, dy, s[1], s[2]))
    if outs:
        mx, my = middle(c)
        print(
            "  (%.0f, %.0f) lvl %d floor %.1f: %s"
            % (mx, my, c[2], probes.get(c, float("nan")), " ".join(outs))
        )
