"""Puts known faults back into the mod's sources one at a time, runs the tests (scripts\\test.cmd),
restores each file byte for byte with a fresh time so that the build sees it, and reports which
test cases caught each fault. A test written for a bug is proven by catching the old code.

usage: python -I tools/tests/mutate.py <spec.json>
spec: [{"file": "src/features/X.cpp", "old": "...", "new": "...", "why": "..."}, ...]
  "old" must stand exactly once in the file; it is written with LF line endings and matches the
  working tree's CRLF as well.

Each fault prints its "why" and the test summary, then the test cases that failed; a fault no test
caught shows "0 failed". The tests are run once more after the last fault is taken out.
"""

import json
import os
import re
import subprocess
import sys
import time

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
CRLF = "\r\n"
LF = "\n"


def run_tests():
    out = subprocess.run(
        ["cmd.exe", "/c", r"scripts\test.cmd"],
        cwd=REPO,
        capture_output=True,
        text=True,
        errors="replace",
    )
    text = out.stdout + out.stderr
    cases = re.findall(r"test cases:.*", text)
    failed = sorted(set(m.strip() for m in re.findall(r"TEST CASE:\s*(.*)", text)))
    return (cases[-1] if cases else "no summary (build failed?)"), failed


def with_endings(s, text):
    # The working tree keeps Windows line endings: a spec written with LF matches them too.
    if CRLF in text:
        return s.replace(CRLF, LF).replace(LF, CRLF)
    return s


def main():
    if len(sys.argv) > 1 and sys.argv[1] in ("-h", "--help"):
        print(__doc__)
        sys.exit(0)
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    spec = json.load(open(sys.argv[1], encoding="utf-8"))
    caught = 0
    applied = 0
    for m in spec:
        path = os.path.join(REPO, m["file"])
        original = open(path, "rb").read()
        text = original.decode("utf-8")
        old = with_endings(m["old"], text)
        new = with_endings(m["new"], text)
        if text.count(old) != 1:
            print("NOT APPLIED (found %d times): %s" % (text.count(old), m["why"]))
            continue
        try:
            open(path, "wb").write(text.replace(old, new).encode("utf-8"))
            summary, failed = run_tests()
            applied += 1
            if failed:
                caught += 1
            print("%s: %s" % (m["why"], summary))
            for f in failed:
                print("    " + f)
        finally:
            open(path, "wb").write(original)
            now = time.time()
            os.utime(path, (now, now))
    summary, failed = run_tests()
    print("restored: %s" % summary)
    print(
        "caught %d of %d faults applied (%d in the spec)" % (caught, applied, len(spec))
    )
    return 0 if caught == applied == len(spec) and not failed else 1


if __name__ == "__main__":
    sys.exit(main())
