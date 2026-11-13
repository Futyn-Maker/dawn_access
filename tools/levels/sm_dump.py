"""Dump SMED2 Lua state machines compactly: per state, its funcList, nextState and conditional jumps,
with states unreachable from the start state marked DEAD.
usage: python -I tools/levels/sm_dump.py <lua file or dir> ...
(the scripts as tools/levels/lua_scripts.py writes them out)"""

import os
import re
import sys

BS = chr(92)


def short_cond(c):
    c = re.sub(r"\s+", " ", c).strip()
    c = c.replace("(sm, ", "(").replace("{", "").replace("}", "")
    return c[:160]


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
            jumps = []
            for jm in re.finditer(
                r"if (.*?) then\s*\n\s*-- Auto-generated JumpToState\(\)[^\n]*\n\s*M_JumpToState\(sm,\s*(\d+)\)",
                b,
                re.S,
            ):
                jumps.append((short_cond(jm.group(1)), int(jm.group(2))))
            # unconditional jumps (rare)
            nj = len(re.findall(r"M_JumpToState\(sm,\s*\d+\)", b))
            if nj != len(jumps):
                for jm in re.finditer(r"M_JumpToState\(sm,\s*(\d+)\)", b):
                    if not any(j[1] == int(jm.group(1)) for j in jumps):
                        jumps.append(("?", int(jm.group(1))))
            states.append((None if nxt == "nil" else int(nxt), fl, jumps))
        # reachability
        reach = set()
        todo = [start]
        while todo:
            s = todo.pop()
            if s in reach or s < 1 or s > len(states):
                continue
            reach.add(s)
            nxt, fl, jumps = states[s - 1]
            if nxt:
                todo.append(nxt)
            for _, j in jumps:
                todo.append(j)
        out.append("### SM %s (start %d)" % (name, start))
        for i, (nxt, fl, jumps) in enumerate(states, 1):
            dead = "" if i in reach else " DEAD"
            out.append("  [%d]%s %s  => %s" % (i, dead, fl, nxt))
            for c, j in jumps:
                out.append("        if %s -> %d" % (c, j))
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
