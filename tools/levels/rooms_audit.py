r"""Audits the rooms the game's reverb volumes lay out (features/Rooms) against an act's levels: the
room each use location lies in, and for every door the rooms its two sides lie in, which must
differ for the exploration list's room rule to tell the two sides apart. Also prints, for every act
with reverb volumes, the rooms' keys and whether the table of room names names them.

python -I tools/levels/rooms_audit.py --zones <act audio tsv> [--levels <act levels json root>] [--names <rooms ini>]

The zones come from level_triggers.py run over the act's LV_reverb (research/triggers/audio/<act>.tsv);
the levels from tools/assets/tojson.py --mappings BatesPlay (the play-time mappings, so that the use
locations' Blueprint properties come out as properties).
"""

import argparse
import glob
import json
import os
import re

import numpy as np
from scipy.spatial import Delaunay


def key_of(name):
    """The room's key, as rooms::Key gives it (features/Rooms.cpp)."""
    rest = name
    if rest.lower().startswith("t_"):
        rest = rest[2:]
    broad = False
    low = rest.lower()
    if low.startswith("globalreverb_") or low.startswith("reverbglobal_"):
        broad, rest = True, rest[13:]
    elif low.startswith("reverb_global_"):
        broad, rest = True, rest[14:]
    elif low.startswith("reverb_"):
        rest = rest[7:]
    else:
        return "", False
    words = []
    for part in re.split(r"[_ \-]+", rest):
        words += [
            w.lower()
            for w in re.findall(
                r"[A-Z]+(?=[A-Z][a-z])|[A-Z]?[a-z]+|[A-Z]+|[0-9]+", part
            )
        ]
    while len(words) > 1 and (words[-1].isdigit() or len(words[-1]) == 1):
        words.pop()
    return "_".join(words), broad


def load_zones(path):
    hulls, names = {}, []
    for line in open(path, encoding="utf-8"):
        p = line.split()
        if not p:
            continue
        if (
            p[0] == "L"
            and p[3] in ("BatesCharacterTriggerVolume", "BP_CharacterTriggerVolume_C")
            and p[2] == "LV_reverb"
        ):
            names.append(p[1])
        elif p[0] == "V" and p[2] == "H":
            hulls.setdefault(p[1], []).append(
                np.array([float(v) for v in p[3:]]).reshape(-1, 3)
            )
    zones = []
    for n in names:
        if n not in hulls:
            continue
        pts = np.vstack(hulls[n])
        key, broad = key_of(n.split("#")[0])
        zones.append(
            {
                "name": n,
                "key": key,
                "broad": broad,
                "tri": [Delaunay(h) for h in hulls[n]],
                "low": pts.min(0),
                "high": pts.max(0),
                "size": float(np.prod(pts.max(0) - pts.min(0))),
            }
        )
    return zones


def zone_at(zones, p):
    best = None
    for z in zones:
        if np.any(p < z["low"]) or np.any(p > z["high"]):
            continue
        if best is not None and z["size"] >= best["size"]:
            continue
        if any(t.find_simplex(p) >= 0 for t in z["tri"]):
            best = z
    return best


def fields(value):
    return {f.get("Name"): f for f in value} if isinstance(value, list) else {}


def vec(prop):
    v = prop.get("Value") if prop else None
    if isinstance(v, list) and v:
        v = v[0].get("Value")
    if isinstance(v, dict) and "X" in v:
        return np.array([float(v["X"]), float(v["Y"]), float(v["Z"])])
    return None


def use_locations(levels_root):
    out = []
    for path in sorted(
        glob.glob(os.path.join(levels_root, "**", "*.json"), recursive=True)
    ):
        if os.path.basename(path).startswith("LV_reverb"):
            continue
        try:
            d = json.load(open(path, encoding="utf-8"))
        except Exception:
            continue
        imps, ex = d["Imports"], d["Exports"]

        def cname(e):
            c = e.get("ClassIndex")
            if isinstance(c, int) and c < 0:
                return imps[-c - 1].get("ObjectName")
            if isinstance(c, int) and c > 0:
                return ex[c - 1].get("ObjectName")
            return ""

        level = os.path.splitext(os.path.basename(path))[0]
        for i, e in enumerate(ex):
            if cname(e) not in ("BP_BatesUseLocation_C", "BatesUseLocation"):
                continue
            props = fields(e.get("Data"))
            root = props.get("RootComponent", {}).get("Value")
            if not isinstance(root, int) or root <= 0:
                continue
            at = vec(fields(ex[root - 1].get("Data")).get("RelativeLocation"))
            if at is None:
                continue
            interaction = props.get("InteractionActor", {}).get("Value")
            kind, actor = "", ""
            if isinstance(interaction, int) and interaction > 0:
                actor = ex[interaction - 1].get("ObjectName")
                kind = cname(ex[interaction - 1])
            core = ""
            for c in ex:
                if (
                    c.get("OuterIndex") == i + 1
                    and cname(c) == "BatesCorePropertiesComponent"
                ):
                    core = str(
                        fields(c.get("Data")).get("CoreName", {}).get("Value") or ""
                    )
            out.append(
                {
                    "level": level,
                    "name": core or e.get("ObjectName"),
                    "at": at,
                    "actor": level + ":" + actor if actor else "",
                    "kind": kind,
                }
            )
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--zones", required=True)
    ap.add_argument("--levels")
    ap.add_argument("--names")
    args = ap.parse_args()
    zones = load_zones(args.zones)
    named = set()
    if args.names:
        for line in open(args.names, encoding="utf-8-sig"):
            m = re.match(r"room\.([a-z0-9_]+)=", line.strip())
            if m:
                named.add(m.group(1))
    keys = {}
    for z in zones:
        keys.setdefault(z["key"], []).append(z["name"])
    print("rooms: %d volumes, %d keys" % (len(zones), len(keys)))
    for k in sorted(keys):
        broad = any(z["broad"] for z in zones if z["key"] == k)
        print(
            "  %-40s %s%s  <- %s"
            % (
                k,
                "named" if k in named else "NO NAME",
                " broad" if broad else "",
                ", ".join(keys[k]),
            )
        )
    # Volumes of two rooms that overlap much: the narrower takes the overlap.
    rooms = [z for z in zones if not z["broad"]]
    for a in range(len(rooms)):
        for b in range(a + 1, len(rooms)):
            za, zb = rooms[a], rooms[b]
            lo, hi = (
                np.maximum(za["low"], zb["low"]),
                np.minimum(za["high"], zb["high"]),
            )
            if np.any(hi <= lo) or za["key"] == zb["key"]:
                continue
            share = float(np.prod(hi - lo)) / min(za["size"], zb["size"])
            if share > 0.25:
                print(
                    "  bounds overlap %.0f%% of the narrower: %s / %s"
                    % (share * 100, za["name"], zb["name"])
                )
    if not args.levels:
        return
    uses = use_locations(args.levels)
    print("use locations: %d" % len(uses))
    rows = []
    for u in uses:
        z = zone_at(zones, u["at"] + np.array([0.0, 0.0, 50.0])) or zone_at(
            zones, u["at"]
        )
        u["zone"] = z
        rows.append((z["name"] if z else "-", u["name"], u["kind"]))
    none = [r for r in rows if r[0] == "-"]
    print("  in no room: %d of %d" % (len(none), len(rows)))
    for r in sorted(rows):
        print("  %-45s %-55s %s" % r)
    doors = {}
    for u in uses:
        if "door" in u["kind"].lower() and u["actor"]:
            doors.setdefault(u["actor"], []).append(u)
    print("doors with their sides: %d" % len(doors))
    same = 0
    for actor, sides in sorted(doors.items()):
        zs = [s["zone"]["name"] if s["zone"] else "-" for s in sides]
        ks = [s["zone"]["key"] if s["zone"] else "-" for s in sides]
        verdict = (
            "one side only"
            if len(sides) < 2
            else ("SAME ROOM" if len(set(ks)) == 1 else "apart")
        )
        if verdict == "SAME ROOM":
            same += 1
        print(
            "  %-14s %-60s %s"
            % (
                verdict,
                actor,
                " | ".join("%s: %s" % (s["name"], z) for s, z in zip(sides, zs)),
            )
        )
    print("doors whose sides lie in one room: %d" % same)


if __name__ == "__main__":
    main()
