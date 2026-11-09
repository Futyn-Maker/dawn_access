"""Lists the Blueprint bytecode of a widget or Blueprint converted to JSON (tools/assets/tojson.py):
one line per expression, nested expressions indented, with the names, strings and numbers each
carries; how a Blueprint decides what the game does (its BP_ events, its timers, what it calls).

usage: python -I tools/assets/bytecode.py <asset.json> [function name filter]
"""
import json, sys
# Compact listing of Blueprint bytecode from a UAssetGUI JSON: one line per expression,
# nested expressions indented; shows the names, strings and numbers each carries.
path = sys.argv[1]
want = sys.argv[2].lower() if len(sys.argv) > 2 else None
d = json.load(open(path, encoding='utf-8'))
exports = d["Exports"]
imports = d.get("Imports", [])
def refname(i):
    try:
        i = int(i)
    except Exception:
        return str(i)
    if i > 0 and i - 1 < len(exports): return exports[i - 1].get("ObjectName", "?")
    if i < 0 and -i - 1 < len(imports): return imports[-i - 1].get("ObjectName", "?")
    return str(i)
def short(t):
    t = str(t)
    if "Expressions." in t: t = t.split("Expressions.")[1]
    return t.split(",")[0]
def bits_of(node):
    out = []
    for k, v in node.items():
        if k in ("$type", "Parameters", "AssignmentExpression", "ReturnExpression", "Expression", "Variable", "DestinationProperty", "ObjectExpression", "BooleanExpression", "ContextExpression", "DelegateExpression", "Cases", "DefaultTerm", "IndexTerm", "Elements", "Value" ): continue
        if isinstance(v, (int, float, str, bool)):
            if k in ("StackNode", "ObjectPath", "StructValue", "Struct", "ClassValue", "Class"): out.append("%s=%s" % (k, refname(v)))
            else: out.append("%s=%s" % (k, str(v)[:80]))
    v = node.get("Value")
    if isinstance(v, (int, float, str, bool)): out.append("value=%s" % str(v)[:100])
    elif isinstance(v, dict) and "Value" in v and isinstance(v["Value"], (str, int, float)): out.append("value=%s" % str(v["Value"])[:100])
    elif isinstance(v, dict):
        for kk in ("CultureInvariantString", "SourceString", "TableId", "Key"):
            if kk in v and isinstance(v[kk], str): out.append("%s=%s" % (kk, v[kk][:100]))
        if "Value" in v and isinstance(v["Value"], dict):
            for kk in ("CultureInvariantString", "SourceString", "TableId", "Key"):
                if kk in v["Value"] and isinstance(v["Value"][kk], str): out.append("%s=%s" % (kk, v["Value"][kk][:100]))
    for k in ("Variable", "DestinationProperty"):
        p = node.get(k)
        if isinstance(p, dict):
            new = p.get("New") or {}
            path = new.get("Path") if isinstance(new, dict) else None
            if path: out.append("%s=%s" % (k.lower(), "/".join(path)))
            elif "Old" in p: out.append("%s=%s" % (k.lower(), refname(p["Old"])))
    return out
def walk(node, depth, out):
    if isinstance(node, dict):
        t = node.get("$type", "")
        if "Expressions." in t:
            out.append("  " * depth + short(t) + " " + " ".join(bits_of(node)))
            depth += 1
        for k, v in node.items():
            if k in ("$type", "DestinationProperty"): continue
            if k == "Variable" and not (isinstance(v, dict) and "Expressions." in str(v.get("$type", ""))): continue
            walk(v, depth, out)
    elif isinstance(node, list):
        for v in node: walk(v, depth, out)
for e in exports:
    sb = e.get("ScriptBytecode")
    if not sb: continue
    name = e.get("ObjectName", "?")
    if want and want not in name.lower(): continue
    out = []
    walk(sb, 0, out)
    print("==== %s" % name)
    for line in out: print(line)
