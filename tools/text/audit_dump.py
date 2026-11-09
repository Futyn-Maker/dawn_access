"""The unread-text report: the texts a screen dump shows that no SAY line of the log spoke
within a window before the dump.

python -I tools/text/audit_dump.py <screen-dump.txt> <DawnAccess.log> [--window 90] [--all]

The dump's time comes from its file name (screen-YYYYMMDD-HHMMSS.txt); SAY lines of the log
with a time within the window before it count as spoken. A dump text counts as spoken when it
appears, whole, inside any spoken line (case-insensitive, whitespace collapsed). --all prints
the spoken texts too.
"""

import argparse
import os
import re
import sys

DUMP_TEXT = re.compile(r'^\s*- \S+ \S+ = "(.*)"')
SAY_LINE = re.compile(r'^\[(\d\d):(\d\d):(\d\d)\.\d+\] \[info \] SAY \S+ "(.*)"$')
NAME_TIME = re.compile(r"screen-\d{8}-(\d\d)(\d\d)(\d\d)")


def norm(text):
    return re.sub(r"\s+", " ", text).strip().lower()


def seconds(h, m, s):
    return int(h) * 3600 + int(m) * 60 + int(s)


def main():
    # The game's texts in every language, whatever the console's code page.
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    ap = argparse.ArgumentParser()
    ap.add_argument("dump")
    ap.add_argument("log")
    ap.add_argument(
        "--window", type=int, default=90, help="seconds before the dump that count"
    )
    ap.add_argument("--all", action="store_true")
    args = ap.parse_args()

    m = NAME_TIME.search(os.path.basename(args.dump))
    if not m:
        sys.exit("the dump's name carries no time: " + args.dump)
    dump_at = seconds(*m.groups())

    texts = []
    with open(args.dump, encoding="utf-8-sig") as f:
        for line in f:
            t = DUMP_TEXT.match(line.rstrip("\n"))
            if t and t.group(1).strip():
                texts.append(t.group(1).strip())

    spoken = []
    with open(args.log, encoding="utf-8", errors="replace") as f:
        for line in f:
            s = SAY_LINE.match(line.rstrip("\n"))
            if not s:
                continue
            at = seconds(s.group(1), s.group(2), s.group(3))
            if dump_at - args.window <= at <= dump_at + 2:
                spoken.append(norm(s.group(4)))

    unread = []
    read = []
    for text in texts:
        n = norm(text)
        if any(n in line for line in spoken):
            read.append(text)
        else:
            unread.append(text)

    print(
        "dump %s: %d texts, %d SAY lines in the last %d s"
        % (os.path.basename(args.dump), len(texts), len(spoken), args.window)
    )
    if args.all:
        for text in read:
            print("  spoken:  %s" % text)
    for text in unread:
        print("  UNREAD:  %s" % text)
    print("unread: %d of %d" % (len(unread), len(texts)))
    return 1 if unread else 0


if __name__ == "__main__":
    sys.exit(main())
