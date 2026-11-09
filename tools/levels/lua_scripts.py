r"""Writes out the Lua source of the game's scene scripts (BMLua): each script asset
(levels/game/<act>/Scripts/**, extracted as legacy files by tools/assets/extract.py) carries its
source as one string of its export, an FString (a 32-bit length that counts the closing zero, then
the text); most are state machines exported from the designers' editor (SMED2).

usage: python -I tools/levels/lua_scripts.py <legacy root> <out root>
  every .uexp under the legacy root whose export holds a Lua source is written to the out root as
  <same path>.lua; the source is the longest string of the export that reads as text.

The scene scripts are read by the mod's offline audit of the scene machines (tests/audit
ScriptsAudit.cpp), by sm_dump.py here, and by anyone reading what a scene waits for.
"""

import os
import struct
import sys


def is_text(data):
    """Whether bytes read as a script's text: printable ASCII, tabs and line ends, or UTF-8."""
    try:
        text = data.decode("utf-8")
    except UnicodeDecodeError:
        return False
    return all(c in "\t\r\n" or ord(c) >= 32 for c in text)


def lua_source(data):
    """The longest length-prefixed string of an export that reads as Lua: the script's source."""
    best = None
    for at in range(0, len(data) - 4):
        n = struct.unpack_from("<i", data, at)[0]
        if n < 16 or at + 4 + n > len(data) or data[at + 4 + n - 1] != 0:
            continue
        body = data[at + 4 : at + 4 + n - 1]
        if (
            (best is None or len(body) > len(best))
            and is_text(body)
            and (b"function" in body or b"--" in body)
        ):
            best = body
    return best


def main():
    if len(sys.argv) != 3:
        print(__doc__)
        return 1
    legacy, out = sys.argv[1], sys.argv[2]
    written = skipped = 0
    for root, _, files in os.walk(legacy):
        for name in files:
            if not name.endswith(".uexp"):
                continue
            path = os.path.join(root, name)
            source = lua_source(open(path, "rb").read())
            if source is None:
                skipped += 1
                continue
            rel = os.path.relpath(path, legacy)
            target = os.path.join(out, os.path.splitext(rel)[0] + ".lua")
            os.makedirs(os.path.dirname(target), exist_ok=True)
            with open(target, "wb") as f:
                f.write(source)
            written += 1
    print("written %d scripts, %d exports without a Lua source" % (written, skipped))
    return 0


if __name__ == "__main__":
    sys.exit(main())
