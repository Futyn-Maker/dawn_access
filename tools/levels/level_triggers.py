r"""Places the collision of every character trigger volume of some levels in the world, from the
levels' JSON (converted with the play-time mappings: tools/assets/tojson.py --mappings BatesPlay)
and the trigger meshes' JSON (trigger_shapes.py reads the same): each volume's mesh component, its mesh's convex
pieces and boxes, the component's placement. Writes the ground record's own "V" lines
(features/GroundFile), so that the mod's offline audit (tests/audit) can judge each volume over a
recorded ground, and "O" lines with the box the mod used before 2026-11-02 (the mesh's local
bounds, turned by the component's yaw) to set beside it.

python -I tools/levels/level_triggers.py <levels json root> <meshes json root> <out.tsv>

Lines:
  L name level class                       where a volume comes from
  V name H x y z ...                       a convex piece of its collision, in the world
  O name mx my mz sx sy sz yaw             the old box: middle, whole sizes, yaw in degrees
"""

import glob
import json
import math
import os
import sys

TRIGGER_CLASSES = {
    "BatesCharacterTriggerVolume",
    "BP_CharacterTriggerVolume_C",
    "BP_CharacterTriggerVolume_LocoSpline_C",
}


def num(x):
    return float(x)


def struct_fields(value):
    return {f.get("Name"): f for f in value} if isinstance(value, list) else {}


def read_vec(prop, default):
    if not prop:
        return default
    v = prop.get("Value")
    if isinstance(v, list) and v:
        v = v[0].get("Value")
    if isinstance(v, dict) and "X" in v:
        return (num(v["X"]), num(v["Y"]), num(v["Z"]))
    return default


def read_rot(prop):
    if not prop:
        return (0.0, 0.0, 0.0)
    v = prop.get("Value")
    if isinstance(v, list) and v:
        v = v[0].get("Value")
    if isinstance(v, dict) and "Pitch" in v:
        return (num(v["Pitch"]), num(v["Yaw"]), num(v["Roll"]))
    return (0.0, 0.0, 0.0)


def quat_from_rotator(pitch, yaw, roll):
    h = math.pi / 360.0
    sp, cp = math.sin(pitch * h), math.cos(pitch * h)
    sy, cy = math.sin(yaw * h), math.cos(yaw * h)
    sr, cr = math.sin(roll * h), math.cos(roll * h)
    return (
        cr * sp * sy - sr * cp * cy,
        -cr * sp * cy - sr * cp * sy,
        cr * cp * sy - sr * sp * cy,
        cr * cp * cy + sr * sp * sy,
    )


def turn(q, v):
    x, y, z, w = q
    ax = (x, y, z)

    def cross(a, b):
        return (
            a[1] * b[2] - a[2] * b[1],
            a[2] * b[0] - a[0] * b[2],
            a[0] * b[1] - a[1] * b[0],
        )

    t = tuple(2.0 * c for c in cross(ax, v))
    c2 = cross(ax, t)
    return (v[0] + w * t[0] + c2[0], v[1] + w * t[1] + c2[1], v[2] + w * t[2] + c2[2])


def place(q, loc, scale, p):
    s = (p[0] * scale[0], p[1] * scale[1], p[2] * scale[2])
    r = turn(q, s)
    return (r[0] + loc[0], r[1] + loc[1], r[2] + loc[2])


def mesh_hulls(path):
    d = json.load(open(path, encoding="utf-8"))
    for e in d["Exports"]:
        c = e.get("ClassIndex")
        name = (
            d["Imports"][-c - 1].get("ObjectName")
            if isinstance(c, int) and c < 0
            else ""
        )
        if name != "BodySetup" or not isinstance(e.get("Data"), list):
            continue
        agg = next((p for p in e["Data"] if p.get("Name") == "AggGeom"), None)
        if not agg:
            return []
        hulls = []
        for part in agg["Value"]:
            if part.get("Name") == "ConvexElems":
                for elem in part["Value"]:
                    f = struct_fields(elem["Value"])
                    pts = [
                        read_vec(v["Value"][0], None) or read_vec(v, None)
                        for v in f["VertexData"]["Value"]
                    ]
                    pts = []
                    for v in f["VertexData"]["Value"]:
                        vv = v["Value"][0]["Value"]
                        pts.append((num(vv["X"]), num(vv["Y"]), num(vv["Z"])))
                    hulls.append(pts)
            elif part.get("Name") == "BoxElems":
                for elem in part["Value"]:
                    f = struct_fields(elem["Value"])
                    centre = read_vec(f.get("Center"), (0.0, 0.0, 0.0))
                    rot = read_rot(f.get("Rotation"))
                    q = quat_from_rotator(*rot)
                    half = (
                        num(f["X"]["Value"]) / 2.0,
                        num(f["Y"]["Value"]) / 2.0,
                        num(f["Z"]["Value"]) / 2.0,
                    )
                    pts = []
                    for sx in (-1, 1):
                        for sy in (-1, 1):
                            for sz in (-1, 1):
                                t = turn(q, (sx * half[0], sy * half[1], sz * half[2]))
                                pts.append(
                                    (
                                        centre[0] + t[0],
                                        centre[1] + t[1],
                                        centre[2] + t[2],
                                    )
                                )
                    hulls.append(pts)
        return hulls
    return []


def main():
    levels_root, meshes_root, out_path = sys.argv[1:4]
    meshes = {}
    for p in glob.glob(os.path.join(meshes_root, "**", "SM_T_*.json"), recursive=True):
        meshes[os.path.splitext(os.path.basename(p))[0]] = p
    out = open(out_path, "w", encoding="utf-8")
    counts = {
        "volumes": 0,
        "placed": 0,
        "no mesh set": 0,
        "mesh not found": 0,
        "attached": 0,
        "raw": 0,
    }
    names = {}
    for path in sorted(
        glob.glob(os.path.join(levels_root, "**", "*.json"), recursive=True)
    ):
        d = json.load(open(path, encoding="utf-8"))
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
            cls = cname(e)
            if cls not in TRIGGER_CLASSES:
                continue
            counts["volumes"] += 1
            if not isinstance(e.get("Data"), list):
                counts["raw"] += 1
                continue
            props = struct_fields(e["Data"])
            core = None
            for j, c in enumerate(ex):
                if (
                    c.get("OuterIndex") == i + 1
                    and cname(c) == "BatesCorePropertiesComponent"
                    and isinstance(c.get("Data"), list)
                ):
                    cp = struct_fields(c["Data"])
                    if "CoreName" in cp:
                        core = str(cp["CoreName"].get("Value"))
            name = core or e.get("ObjectName")
            if name in names:
                names[name] += 1
                name = "%s#%d" % (name, names[name])
            else:
                names[name] = 1
            comp_index = None
            for key in ("TriggerVolume", "RootComponent"):
                v = props.get(key, {}).get("Value")
                if isinstance(v, int) and v > 0:
                    comp_index = v
                    break
            if not comp_index:
                # The component the Blueprint makes keeps its name.
                for j, c in enumerate(ex):
                    if (
                        c.get("OuterIndex") == i + 1
                        and c.get("ObjectName") == "TriggerVolume"
                    ):
                        comp_index = j + 1
            if not comp_index:
                counts["no mesh set"] += 1
                continue
            comp = ex[comp_index - 1]
            cprops = (
                struct_fields(comp.get("Data"))
                if isinstance(comp.get("Data"), list)
                else {}
            )
            if "AttachParent" in cprops:
                counts["attached"] += 1
            mesh_ref = cprops.get("StaticMesh", {}).get("Value")
            mesh_name = (
                imps[-mesh_ref - 1].get("ObjectName")
                if isinstance(mesh_ref, int) and mesh_ref < 0
                else None
            )
            if not mesh_name and cls.startswith("BP_CharacterTriggerVolume"):
                # The Blueprint's own mesh: the engine's cube, 256 cm a side (/Engine/EngineMeshes/Cube).
                mesh_name = "Cube"
            if not mesh_name:
                counts["no mesh set"] += 1
                continue
            if mesh_name == "Cube":
                hulls = [
                    [
                        (sx * 128.0, sy * 128.0, sz * 128.0)
                        for sx in (-1, 1)
                        for sy in (-1, 1)
                        for sz in (-1, 1)
                    ]
                ]
            elif mesh_name not in meshes:
                counts["mesh not found"] += 1
                continue
            else:
                hulls = mesh_hulls(meshes[mesh_name])
            if not hulls:
                counts["mesh not found"] += 1
                continue
            loc = read_vec(cprops.get("RelativeLocation"), (0.0, 0.0, 0.0))
            rot = read_rot(cprops.get("RelativeRotation"))
            scale = read_vec(cprops.get("RelativeScale3D"), (1.0, 1.0, 1.0))
            q = quat_from_rotator(*rot)
            print("L %s %s %s" % (name, level, cls), file=out)
            allpts = []
            for h in hulls:
                world = [place(q, loc, scale, p) for p in h]
                allpts += h
                print(
                    "V %s H %s" % (name, " ".join("%.2f %.2f %.2f" % p for p in world)),
                    file=out,
                )
            lo = [min(p[k] for p in allpts) for k in range(3)]
            hi = [max(p[k] for p in allpts) for k in range(3)]
            offset = tuple((lo[k] + hi[k]) / 2.0 * scale[k] for k in range(3))
            r = math.radians(rot[1])
            middle = (
                loc[0] + offset[0] * math.cos(r) - offset[1] * math.sin(r),
                loc[1] + offset[0] * math.sin(r) + offset[1] * math.cos(r),
                loc[2] + offset[2],
            )
            size = tuple((hi[k] - lo[k]) * abs(scale[k]) for k in range(3))
            print(
                "O %s %.2f %.2f %.2f %.2f %.2f %.2f %.3f"
                % (name, *middle, *size, rot[1]),
                file=out,
            )
            counts["placed"] += 1
    out.close()
    print(counts)


if __name__ == "__main__":
    main()
