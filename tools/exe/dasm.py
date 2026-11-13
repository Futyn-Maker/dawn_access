"""Disassembles functions of the game's executable, naming the calls and jumps by the PDB's symbols.

usage: python -I tools/exe/dasm.py <function> [<function> ...]
  a function is a name as functions.tsv spells it (UBatesKeyRebindSettingWidget::StopBinding: its
  RVA and length are looked up), or "<rva hex> <length hex>" as two arguments.

functions.tsv is written by pdb_symbols.py dump into research/symbols (or DA_SYMBOLS); the game's
folder is DA_GAME_DIR or Steam's default.
"""

import bisect
import os
import sys

import capstone
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


def is_hex(text):
    try:
        int(text, 16)
        return True
    except ValueError:
        return False


def main():
    if len(sys.argv) > 1 and sys.argv[1] in ("-h", "--help"):
        print(__doc__)
        return 0
    if len(sys.argv) < 2:
        print(__doc__)
        return 1
    rows = load_functions()
    starts = [r[0] for r in rows]
    exact = {}
    by_name = {}
    for rva, length, name in rows:
        exact.setdefault(rva, name)
        by_name.setdefault(name, (rva, length))

    def name_of(rva):
        if rva in exact:
            return exact[rva]
        i = bisect.bisect_right(starts, rva) - 1
        return "%s+0x%x" % (rows[i][2], rva - rows[i][0]) if i >= 0 else "?"

    wanted = []
    args = sys.argv[1:]
    k = 0
    while k < len(args):
        if is_hex(args[k]) and k + 1 < len(args) and is_hex(args[k + 1]):
            wanted.append((int(args[k], 16), int(args[k + 1], 16)))
            k += 2
            continue
        if args[k] not in by_name:
            raise SystemExit("no function %s in %s" % (args[k], FUNCS))
        wanted.append(by_name[args[k]])
        k += 1

    pe = pefile.PE(EXE, fast_load=True)
    image = pe.get_memory_mapped_image()
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
    for rva, length in wanted:
        print("=== %s (%x)" % (name_of(rva), rva))
        for ins in md.disasm(image[rva : rva + length], rva):
            extra = ""
            if ins.mnemonic in ("call", "jmp") and ins.op_str.startswith("0x"):
                extra = "  ; " + name_of(int(ins.op_str, 16))
            print("%x: %s %s%s" % (ins.address, ins.mnemonic, ins.op_str, extra))
    return 0


if __name__ == "__main__":
    sys.exit(main())
