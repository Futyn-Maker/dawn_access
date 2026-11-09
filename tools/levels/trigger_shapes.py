r"""Catalogues the collision of every trigger mesh the game ships (SM_T_*), as UAssetGUI's JSON of
the meshes gives it: the convex pieces, boxes, balls and capsules of each mesh's BodySetup, and how
far the box square with the mesh around them over-reaches the pieces themselves. A volume judged
by that box (the mod's rule before 2026-11-02) is taken for reached where its pieces are not.

python -I tools/levels/trigger_shapes.py <json root> [--acts act1_3 act2_1 ...] [--out shapes.tsv]

For each mesh: the act folder, the mesh, the counts, the box's size, the pieces' own volume, and the
reach of the box beyond the pieces: the largest distance from a point of the box to the pieces,
estimated on a lattice of the box (the corner farthest from a slab set aslant is the worst).
"""

import argparse
import itertools
import json
import math
import os
import sys


def num(x):
    # UAssetGUI writes a zero as "+0".
    return float(x)


def vec(v):
    return (num(v["X"]), num(v["Y"]), num(v["Z"]))


def fields(struct_value):
    return {f["Name"]: f for f in struct_value}


def read_shape(path):
    d = json.load(open(path, encoding="utf-8"))
    setup = None
    for e in d["Exports"]:
        c = e.get("ClassIndex")
        name = (
            d["Imports"][-c - 1].get("ObjectName")
            if isinstance(c, int) and c < 0
            else ""
        )
        if name == "BodySetup":
            setup = e
            break
    if not setup or not isinstance(setup.get("Data"), list):
        return None
    agg = next((p for p in setup.get("Data", []) if p.get("Name") == "AggGeom"), None)
    shape = {"hulls": [], "boxes": [], "balls": 0, "pills": 0, "complex": False}
    for p in setup.get("Data", []):
        if p.get("Name") == "CollisionTraceFlag":
            shape["complex"] = "UseComplexAsSimple" in str(p.get("Value"))
    if not agg:
        return shape
    for part in agg["Value"]:
        n = part.get("Name")
        if n == "ConvexElems":
            for elem in part["Value"]:
                f = fields(elem["Value"])
                pts = [vec(v["Value"][0]["Value"]) for v in f["VertexData"]["Value"]]
                shape["hulls"].append(pts)
        elif n == "BoxElems":
            for elem in part["Value"]:
                f = fields(elem["Value"])
                shape["boxes"].append(f)
        elif n == "SphereElems":
            shape["balls"] += len(part["Value"])
        elif n == "SphylElems":
            shape["pills"] += len(part["Value"])
    return shape


def planes_of(points):
    """The face planes of the convex hull of a few points, by brute force: every plane through
    three points with all points on one side (n, d with n.p <= d inside)."""
    planes = []
    for a, b, c in itertools.combinations(points, 3):
        ab = [b[i] - a[i] for i in range(3)]
        ac = [c[i] - a[i] for i in range(3)]
        n = [
            ab[1] * ac[2] - ab[2] * ac[1],
            ab[2] * ac[0] - ab[0] * ac[2],
            ab[0] * ac[1] - ab[1] * ac[0],
        ]
        length = math.sqrt(sum(x * x for x in n))
        if length < 1e-6:
            continue
        n = [x / length for x in n]
        d = sum(n[i] * a[i] for i in range(3))
        side = [sum(n[i] * p[i] for i in range(3)) - d for p in points]
        if all(s <= 1e-3 for s in side):
            planes.append((n, d))
        elif all(s >= -1e-3 for s in side):
            planes.append(([-x for x in n], -d))
    return planes


def outside(planes, p):
    """How far a point lies outside the hull by its planes (a lower bound of the distance)."""
    return max(0.0, max(sum(n[i] * p[i] for i in range(3)) - d for n, d in planes))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("root")
    ap.add_argument("--acts", nargs="*", default=None)
    ap.add_argument("--out")
    args = ap.parse_args()
    rows = []
    for dirpath, _, files in os.walk(args.root):
        for f in files:
            if not (f.startswith("SM_T_") and f.endswith(".json")):
                continue
            rel = os.path.relpath(os.path.join(dirpath, f), args.root).replace(
                "\\", "/"
            )
            parts = rel.split("/")
            act = parts[parts.index("game") + 1] if "game" in parts else parts[0]
            if args.acts and act.lower() not in [a.lower() for a in args.acts]:
                continue
            shape = read_shape(os.path.join(dirpath, f))
            if shape is None:
                rows.append((act, f[:-5], "no-setup", 0, 0, 0, 0, 0.0, 0.0))
                continue
            pts = [p for h in shape["hulls"] for p in h]
            reach = 0.0
            size = (0.0, 0.0, 0.0)
            if pts and not shape["boxes"] and not shape["balls"] and not shape["pills"]:
                lo = [min(p[i] for p in pts) for i in range(3)]
                hi = [max(p[i] for p in pts) for i in range(3)]
                size = tuple(hi[i] - lo[i] for i in range(3))
                hulls = [planes_of(h) for h in shape["hulls"]]
                # The farthest point of the box (on a lattice of it) from the pieces.
                steps = 6
                for ix in range(steps + 1):
                    for iy in range(steps + 1):
                        for iz in (0, steps // 2, steps):
                            p = [
                                lo[0] + (hi[0] - lo[0]) * ix / steps,
                                lo[1] + (hi[1] - lo[1]) * iy / steps,
                                lo[2] + (hi[2] - lo[2]) * iz / steps,
                            ]
                            reach = max(
                                reach,
                                min(outside(pl, p) for pl in hulls) if hulls else 0.0,
                            )
            kind = (
                "complex"
                if shape["complex"]
                else ("hulls" if shape["hulls"] else "other")
            )
            rows.append(
                (
                    act,
                    f[:-5],
                    kind,
                    len(shape["hulls"]),
                    sum(len(h) for h in shape["hulls"]),
                    len(shape["boxes"]),
                    shape["balls"] + shape["pills"],
                    max(size),
                    reach,
                )
            )
    rows.sort(key=lambda r: (r[0].lower(), -r[8]))
    out = open(args.out, "w", encoding="utf-8") if args.out else sys.stdout
    print(
        "act\tmesh\tkind\thulls\tpoints\tboxes\tballs+pills\tsize\tbox reach beyond",
        file=out,
    )
    for r in rows:
        print("%s\t%s\t%s\t%d\t%d\t%d\t%d\t%.0f\t%.0f" % r, file=out)
    if args.out:
        out.close()
    # A summary per act: how many meshes, how many the box over-reaches by more than 30 cm.
    acts = {}
    for r in rows:
        a = acts.setdefault(r[0], [0, 0, 0, 0])
        a[0] += 1
        if r[8] > 30.0:
            a[1] += 1
        if r[3] > 1:
            a[2] += 1
        if r[2] != "hulls":
            a[3] += 1
    print(
        "act\tmeshes\tbox over-reaches >30 cm\tseveral pieces\tnot hulls only",
        file=sys.stderr,
    )
    for act, a in sorted(acts.items(), key=lambda kv: kv[0].lower()):
        print("%s\t%d\t%d\t%d\t%d" % (act, *a), file=sys.stderr)


if __name__ == "__main__":
    main()
