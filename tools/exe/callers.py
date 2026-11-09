"""Finds the direct call sites (E8 rel32) and tail jumps (E9 rel32) to functions of the game's
executable, and names the function each lies in. A function the compiler inlined everywhere has
none; one reached only through a vtable or a delegate has none either.

usage: python -I tools/exe/callers.py <function> [<function> ...]
  a function is its RVA in hex or its name as functions.tsv spells it.

functions.tsv is written by pdb_symbols.py dump into research/symbols (or DA_SYMBOLS); the game's
folder is DA_GAME_DIR or Steam's default.
"""

import bisect
import os
import sys

import numpy as np
import pefile

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
SYMBOLS = os.environ.get("DA_SYMBOLS", os.path.join(REPO, "research", "symbols"))
FUNCS = os.path.join(SYMBOLS, "functions.tsv")
GAME = os.environ.get(
    "DA_GAME_DIR", r"C:\Program Files (x86)\Steam\steamapps\common\Until Dawn"
)
EXE = os.path.join(GAME, r"Windows\Bates\Binaries\Win64\Bates-Win64-Shipping.exe")


def load_functions():
    rows = []
    with open(FUNCS, encoding="utf-8", errors="replace") as f:
        next(f)
        for line in f:
            p = line.rstrip("\n").split("\t")
            try:
                rows.append((int(p[0], 16), int(p[1], 16), p[2]))
            except (ValueError, IndexError):
                pass
    rows.sort(key=lambda r: r[0])
    return rows


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 1
    rows = load_functions()
    starts = [r[0] for r in rows]
    by_name = {}
    for rva, _, name in rows:
        by_name.setdefault(name, rva)

    def owner(rva):
        i = bisect.bisect_right(starts, rva) - 1
        if i >= 0 and rows[i][0] <= rva < rows[i][0] + max(rows[i][1], 1):
            return "%s+0x%x" % (rows[i][2], rva - rows[i][0])
        return "?"

    pe = pefile.PE(EXE, fast_load=True)
    text = next(s for s in pe.sections if s.Name.rstrip(b"\0") == b".text")
    base = text.VirtualAddress
    data = np.frombuffer(text.get_data(), dtype=np.uint8)
    found = {}
    for opcode, kind in ((0xE8, "call"), (0xE9, "jmp")):
        idx = np.nonzero(data[:-5] == opcode)[0]
        rel = (
            data[idx + 1].astype(np.int64)
            | (data[idx + 2].astype(np.int64) << 8)
            | (data[idx + 3].astype(np.int64) << 16)
            | (data[idx + 4].astype(np.int64) << 24)
        )
        rel = np.where(rel >= 2**31, rel - 2**32, rel)
        found[kind] = (idx, base + idx + 5 + rel)

    for spec in sys.argv[1:]:
        try:
            want = int(spec, 16)
        except ValueError:
            if spec not in by_name:
                raise SystemExit("no function %s in %s" % (spec, FUNCS))
            want = by_name[spec]
        for kind in ("call", "jmp"):
            idx, targets = found[kind]
            hits = idx[targets == want]
            # A jump is only counted from code that lies inside a function the PDB knows: the byte
            # E9 is common in data and in the middle of other instructions.
            sites = [base + h for h in hits if kind == "call" or owner(base + h) != "?"]
            print("== %x %s: %d %s(s)" % (want, owner(want), len(sites), kind))
            for site in sites:
                print("   %x in %s" % (site, owner(site)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
