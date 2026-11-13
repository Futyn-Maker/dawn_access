"""Print the stage graph of every activity in UAssetGUI level JSON files: for each stage its class, the
key settings, and every outcome it can take (choice options, QTE success/failure, shooting targets,
timeouts, conditional branches), following the outcome and condition subobjects to their next stage
names and their conditions.
usage: python -I tools/levels/stage_graph.py <json file or dir> [activity-name-substring]
(levels converted with tools/assets/tojson.py --mappings BatesPlay)"""

import json
import os
import sys

if len(sys.argv) > 1 and sys.argv[1] in ("-h", "--help"):
    print(__doc__)
    sys.exit(0)
if len(sys.argv) < 2:
    sys.exit(__doc__)
root = sys.argv[1]
filt = sys.argv[2] if len(sys.argv) > 2 else None
FOLLOW = ("Outcome", "Condition", "Option", "Action", "Transition")
SKIP = {
    "UCSSerializationIndex",
    "CreationMethod",
    "bNetAddressable",
    "CameraSequence",
    "NeutralConversationFacialExpressionOveride",
    "bUseSeparateInterpolationSpeedForDecay",
    "CameraSequenceDecayInterpolationSpeed",
    "bBlendHeadLookOn",
    "bOverrideInitialHeadLookAngles",
    "bShouldUseHeadLook",
    "StartMarkerName",
    "EndMarkerName",
}


def files(r):
    if os.path.isfile(r):
        return [r]
    out = []
    for dp, _, fs in os.walk(r):
        for f in sorted(fs):
            if f.endswith(".json"):
                out.append(os.path.join(dp, f))
    return sorted(out)


class L:
    def __init__(self, p):
        self.d = json.load(open(p, encoding="utf-8"))
        self.ex = self.d["Exports"]
        self.im = self.d["Imports"]

    def name(self, i):
        if not isinstance(i, int) or i == 0:
            return None
        return self.im[-i - 1]["ObjectName"] if i < 0 else self.ex[i - 1]["ObjectName"]

    def cls(self, k):
        return self.name(self.ex[k - 1].get("ClassIndex")) or "?"

    def outer(self, k):
        return self.ex[k - 1].get("OuterIndex", 0)


def val(lv, p, depth, seen):
    t = p.get("$type", "").split(".")[-1].split(",")[0]
    v = p.get("Value")
    if t == "ObjectPropertyData":
        if isinstance(v, int) and v > 0:
            c = lv.cls(v)
            if any(f in c for f in FOLLOW) and depth < 6 and v not in seen:
                seen = seen | {v}
                return "%s{%s}" % (
                    c.replace("Bates", ""),
                    props(lv, v, depth + 1, seen),
                )
            return lv.name(v)
        return lv.name(v) if isinstance(v, int) else str(v)
    if t in ("ArrayPropertyData", "SetPropertyData"):
        return "[" + ", ".join(val(lv, x, depth, seen) for x in (v or [])) + "]"
    if t == "MapPropertyData":
        out = []
        for kv in v or []:
            try:
                out.append(
                    "%s=>%s"
                    % (val(lv, kv[0], depth, seen), val(lv, kv[1], depth, seen))
                )
            except Exception:
                out.append("?")
        return "{" + ", ".join(out) + "}"
    if t == "StructPropertyData":
        if isinstance(v, list):
            return (
                "("
                + ", ".join(
                    "%s=%s" % (x.get("Name"), val(lv, x, depth, seen))
                    for x in v
                    if isinstance(x, dict)
                )
                + ")"
            )
        return str(v)
    if t == "TextPropertyData":
        return "TEXT(%s)" % (p.get("Value") or p.get("CultureInvariantString"))
    if t == "SoftObjectPropertyData":
        try:
            return v["AssetPath"]["AssetName"].split(".")[-1]
        except Exception:
            return "?"
    if isinstance(v, (str, int, float, bool)) or v is None:
        return str(v)
    return json.dumps(v)[:80]


def props(lv, k, depth=0, seen=frozenset()):
    e = lv.ex[k - 1]
    d = e.get("Data")
    if not isinstance(d, list):
        return "RAW" if "Raw" in e.get("$type", "") else ""
    out = []
    for p in d:
        if not isinstance(p, dict) or p.get("Name") in SKIP:
            continue
        out.append("%s=%s" % (p.get("Name"), val(lv, p, depth, seen)))
    return "; ".join(out)


for f in files(root):
    lv = L(f)
    acts = [
        k
        for k in range(1, len(lv.ex) + 1)
        if "ActivityActor" in lv.cls(k) or lv.cls(k) == "BatesLevelSequenceActor"
    ]
    for a in acts:
        core = None
        for k in range(1, len(lv.ex) + 1):
            if lv.outer(k) == a and lv.cls(k) == "BatesCorePropertiesComponent":
                for p in lv.ex[k - 1].get("Data") or []:
                    if isinstance(p, dict) and p.get("Name") == "CoreName":
                        core = p.get("Value")
        label = core or lv.ex[a - 1]["ObjectName"]
        if filt and filt not in label:
            continue
        stages = [
            k
            for k in range(1, len(lv.ex) + 1)
            if lv.outer(k) == a
            and "Stage" in lv.cls(k)
            and "Outcome" not in lv.cls(k)
            and "Condition" not in lv.cls(k)
        ]
        if not stages:
            continue
        print("## %s (%s) @%s" % (label, lv.cls(a), os.path.basename(f)))
        print("   actor: " + props(lv, a)[:600])
        for s in stages:
            print("   [%s] %s" % (lv.cls(s).replace("Bates", ""), props(lv, s)))
        print()
