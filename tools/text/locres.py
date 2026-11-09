"""Reader for Unreal .locres files (version 3, the optimized CityHash64 layout of UE 5.x).

python -I tools/text/locres.py dump <Game.locres> <out.tsv>        namespace, key, text as a TSV
python -I tools/text/locres.py grep <Game.locres> <regex>           print matching namespace/key/text lines
python -I tools/text/locres.py stats <Game.locres>                  namespaces with their entry counts
"""

import re
import struct
import sys

MAGIC = bytes.fromhex("0E14747567 4A03FC4A15909D C3377F1B".replace(" ", ""))


def fstring(b, o):
    (n,) = struct.unpack_from("<i", b, o)
    o += 4
    if n < 0:
        s = b[o : o - 2 * n - 2].decode("utf-16-le", "replace")
        o += -2 * n
    else:
        s = b[o : o + max(n - 1, 0)].decode("utf-8", "replace")
        o += n
    return s, o


def read(path):
    b = open(path, "rb").read()
    if b[:16] != MAGIC:
        raise SystemExit("not a locres file (magic mismatch): %s" % path)
    version = b[16]
    o = 17
    (strings_offset,) = struct.unpack_from("<q", b, o)
    o += 8
    if version >= 2:
        o += 4  # entries count
    (namespace_count,) = struct.unpack_from("<I", b, o)
    o += 4
    # The string table.
    so = strings_offset
    (count,) = struct.unpack_from("<i", b, so)
    so += 4
    strings = []
    for _ in range(count):
        s, so = fstring(b, so)
        if version >= 2:
            so += 4  # reference count
        strings.append(s)
    entries = []
    for _ in range(namespace_count):
        if version >= 2:
            o += 4  # namespace key hash
        namespace, o = fstring(b, o)
        (key_count,) = struct.unpack_from("<I", b, o)
        o += 4
        for _ in range(key_count):
            if version >= 2:
                o += 4  # key hash
            key, o = fstring(b, o)
            o += 4  # source string hash
            (index,) = struct.unpack_from("<i", b, o)
            o += 4
            entries.append(
                (namespace, key, strings[index] if 0 <= index < len(strings) else "")
            )
    return version, entries


def clean(s):
    return s.replace("\r", "\\r").replace("\n", "\\n").replace("\t", "\\t")


if __name__ == "__main__":
    # The game's texts in every language, whatever the console's code page.
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    cmd = sys.argv[1]
    version, entries = read(sys.argv[2])
    if cmd == "dump":
        with open(sys.argv[3], "w", encoding="utf-8", newline="\n") as f:
            f.write("namespace\tkey\ttext\n")
            for ns, key, text in entries:
                f.write("%s\t%s\t%s\n" % (ns, key, clean(text)))
        print("version", version, "entries", len(entries), "->", sys.argv[3])
    elif cmd == "grep":
        rx = re.compile(sys.argv[3], re.I)
        for ns, key, text in entries:
            if rx.search(text) or rx.search(key) or rx.search(ns):
                print("%s\t%s\t%s" % (ns, key, clean(text)))
    elif cmd == "stats":
        from collections import Counter

        c = Counter(ns for ns, _, _ in entries)
        print("version", version, "entries", len(entries), "namespaces", len(c))
        for ns, n in sorted(c.items(), key=lambda kv: -kv[1]):
            print("%6d %s" % (n, ns))
