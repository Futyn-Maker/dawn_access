"""Dump SMED2 Lua state machines with full state bodies, one statement per line, plus reachability.
usage: python -I tools/levels/sm_full.py <lua file or dir> ...
(the scripts as tools/levels/lua_scripts.py writes them out)"""

import os
import re
import sys

BS = chr(92)


def compact_body(b):
    lines = []
    for raw in b.split("\n"):
        s = raw.strip()
        if (
            not s
            or s.startswith("Debug_SetSMEDStateID")
            or s.startswith("-- Set what the last run state")
        ):
            continue
        if (
            s.startswith("-- Auto-generated JumpToState")
            or s.startswith("--If there was anything")
            or s.startswith("--was dropped")
        ):
            continue
        lines.append((len(raw) - len(raw.lstrip()), s))
    # join continuation lines: track paren/brace depth
    out = []
    buf = ""
    ind = 0
    depth = 0
    for i, s in lines:
        if not buf:
            ind = i
        buf = (buf + " " + s) if buf else s
        depth += s.count("(") + s.count("{") - s.count(")") - s.count("}")
        if depth <= 0:
            depth = 0
            st = buf
            st = st.replace("(sm, {", "(").replace("})", ")").replace("(sm,", "(")
            st = re.sub(r"M_JumpToState\(\s*(\d+)\)", r"GOTO \1", st)
            out.append((ind, st))
            buf = ""
    res = []
    for i, s in out:
        if s in ("end", "return"):
            continue
        res.append("      " + " " * max(0, i - 2) + s)
    return res


def parse(path):
    t = open(path, encoding="latin-1").read()
    funcs = {}
    for m in re.finditer(r"^function (\w+)\(sm\)\n(.*?)^end\n", t, re.S | re.M):
        funcs[m.group(1)] = m.group(2)
    out = []
    for m in re.finditer(
        r"gRegisteredStatemachines\.(\w+) = \{\s*startState = (\d+),\s*name = '([^']*)',\s*states = \{(.*?)\n    \},",
        t,
        re.S,
    ):
        start, name, body = int(m.group(2)), m.group(3), m.group(4)
        pat = (
            r"\{nextState = (\w+),\s*func = (\w+),\s*funcList = '((?:[^'"
            + BS
            + BS
            + r"]|"
            + BS
            + BS
            + r".)*)'\}"
        )
        states = []
        for sm in re.finditer(pat, body):
            nxt, fn, fl = sm.group(1), sm.group(2), sm.group(3)
            b = funcs.get(fn, "")
            jumps = [int(x) for x in re.findall(r"M_JumpToState\(sm,\s*(\d+)\)", b)]
            states.append((None if nxt == "nil" else int(nxt), fl, jumps, b))
        reach = set()
        todo = [start]
        while todo:
            s = todo.pop()
            if s in reach or s < 1 or s > len(states):
                continue
            reach.add(s)
            nxt, fl, jumps, b = states[s - 1]
            if nxt:
                todo.append(nxt)
            todo.extend(jumps)
        out.append("### SM %s (start %d)" % (name, start))
        for i, (nxt, fl, jumps, b) in enumerate(states, 1):
            if i not in reach:
                out.append("  [%d] DEAD: %s" % (i, fl[:100]))
                continue
            out.append("  [%d] => %s" % (i, nxt))
            out.extend(compact_body(b))
    return out


if len(sys.argv) > 1 and sys.argv[1] in ("-h", "--help"):
    print(__doc__)
    sys.exit(0)
if len(sys.argv) < 2:
    sys.exit(__doc__)
for a in sys.argv[1:]:
    paths = []
    if os.path.isdir(a):
        for dp, _, fs in os.walk(a):
            for f in sorted(fs):
                if f.endswith(".lua"):
                    paths.append(os.path.join(dp, f))
    else:
        paths = [a]
    for p in paths:
        print("=" * 4, p.replace(BS, "/").split("levels/game/")[-1])
        for line in parse(p):
            print(line)
