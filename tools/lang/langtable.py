"""Edits every language table of the mod at once, keeping each file's byte order mark, line endings
and the order of its lines.

usage:
  python -I tools/lang/langtable.py insert <after-key> <lines.json> [--folder DIR]
      puts lines after the line of <after-key>; lines.json is a list of "key=value" lines for every
      language, or {"<code>": ["key=value", ...], ...} with every table's code (en_US, ru_RU, ...).
      A key a table has already is refused.
  python -I tools/lang/langtable.py replace <lines.json> [--folder DIR]
      replaces the lines of the keys given, in their place; {"<code>": ["key=value", ...], ...}.
      Each key must stand once in its table.
  python -I tools/lang/langtable.py append <key> <sentences.json> [--folder DIR]
      adds a sentence to one key's value; {"<code>": "sentence", ...}. A sentence is joined without
      a space after a full-width stop (Japanese, Chinese).

The folder is mod/lang of this repository unless given (mod/lang/rooms, mod/lang/totems ...).
Nothing is written unless every table takes the change.
"""

import argparse
import json
import os
import sys

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
TABLES = os.path.join(REPO, "mod", "lang")


def read_table(path):
    raw = open(path, "rb").read()
    bom = raw.startswith(b"\xef\xbb\xbf")
    text = raw[3:].decode("utf-8") if bom else raw.decode("utf-8")
    nl = "\r\n" if "\r\n" in text else "\n"
    return bom, nl, text.split(nl)


def write_table(path, bom, nl, rows):
    open(path, "wb").write(
        (b"\xef\xbb\xbf" if bom else b"") + nl.join(rows).encode("utf-8")
    )


def tables(folder):
    names = sorted(n for n in os.listdir(folder) if n.endswith(".ini"))
    if not names:
        raise SystemExit(f"{folder}: no language tables")
    return [(n[:-4], os.path.join(folder, n)) for n in names]


def lines_of(spec, code, name):
    if isinstance(spec, list):
        return spec
    if code not in spec:
        raise SystemExit(f"{name}: no lines for {code}")
    return spec[code]


def insert(after, spec, folder):
    changed = []
    for code, path in tables(folder):
        name = os.path.basename(path)
        lines = lines_of(spec, code, name)
        bom, nl, rows = read_table(path)
        at = [i for i, r in enumerate(rows) if r.startswith(after + "=")]
        if len(at) != 1:
            raise SystemExit(f"{name}: {len(at)} lines for {after}")
        keys = [line.split("=", 1)[0] for line in lines]
        if any(r.split("=", 1)[0] in keys for r in rows):
            raise SystemExit(f"{name}: already has one of {keys}")
        rows[at[0] + 1 : at[0] + 1] = lines
        changed.append((path, bom, nl, rows, f"{len(lines)} lines after {after}"))
    return changed


def replace(spec, folder):
    changed = []
    for code, path in tables(folder):
        name = os.path.basename(path)
        lines = lines_of(spec, code, name)
        bom, nl, rows = read_table(path)
        for line in lines:
            key = line.split("=", 1)[0] + "="
            at = [i for i, r in enumerate(rows) if r.startswith(key)]
            if len(at) != 1:
                raise SystemExit(f"{name}: {key} stands {len(at)} times")
            rows[at[0]] = line
        changed.append((path, bom, nl, rows, f"{len(lines)} lines replaced"))
    return changed


def append(key, spec, folder):
    changed = []
    for code, path in tables(folder):
        name = os.path.basename(path)
        if code not in spec:
            raise SystemExit(f"{name}: no sentence for {code}")
        bom, nl, rows = read_table(path)
        at = [i for i, r in enumerate(rows) if r.startswith(key + "=")]
        if len(at) != 1:
            raise SystemExit(f"{name}: {len(at)} lines for {key}")
        value = rows[at[0]]
        sentence = spec[code]
        if sentence in value:
            raise SystemExit(f"{name}: already there")
        joiner = "" if value.endswith(("。", "．")) else " "
        rows[at[0]] = value + joiner + sentence
        changed.append((path, bom, nl, rows, f"appended to {key}"))
    return changed


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    sub = parser.add_subparsers(dest="command", required=True)
    p = sub.add_parser("insert")
    p.add_argument("after")
    p.add_argument("spec")
    p.add_argument("--folder", default=TABLES)
    p = sub.add_parser("replace")
    p.add_argument("spec")
    p.add_argument("--folder", default=TABLES)
    p = sub.add_parser("append")
    p.add_argument("key")
    p.add_argument("spec")
    p.add_argument("--folder", default=TABLES)
    args = parser.parse_args()
    spec = json.load(open(args.spec, encoding="utf-8"))
    if args.command == "insert":
        changed = insert(args.after, spec, args.folder)
    elif args.command == "replace":
        changed = replace(spec, args.folder)
    else:
        changed = append(args.key, spec, args.folder)
    for path, bom, nl, rows, what in changed:
        write_table(path, bom, nl, rows)
        print(f"{os.path.basename(path)}: {what}")


if __name__ == "__main__":
    sys.exit(main())
