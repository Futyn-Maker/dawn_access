"""Prints every call of the given SF_ functions (the scene scripts' library: SF_IsInTrigger,
SF_QueueCreditMovie ...) in the scene scripts, with their argument tables flattened.

usage: python -I tools/levels/sf_calls.py <lua root> <name> [...]
(the scripts as tools/levels/lua_scripts.py writes them out)
"""
import os, re, sys
root = sys.argv[1]
names = sys.argv[2:]
pat = re.compile(r'\b(' + '|'.join(re.escape(n) for n in names) + r')\(sm,\s*')
for dp, _, files in os.walk(root):
    for f in sorted(files):
        if not f.endswith('.lua'):
            continue
        p = os.path.join(dp, f)
        t = open(p, encoding='latin-1').read()
        for m in pat.finditer(t):
            i = m.end()
            depth = 1  # inside '('
            j = i
            while j < len(t) and depth:
                c = t[j]
                if c == '(':
                    depth += 1
                elif c == ')':
                    depth -= 1
                j += 1
            args = re.sub(r'\s+', ' ', t[i:j-1])
            rel = os.path.relpath(p, root).replace(os.sep, '/')
            rel = rel.replace('levels/game/', '')
            print(f"{rel}\t{m.group(1)}\t{args}")
