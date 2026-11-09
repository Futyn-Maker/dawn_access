r"""Pulls packages out of the game's IoStore (Bates-Windows.utoc/.ucas, unencrypted) as legacy
.uasset/.umap files with retoc, for tojson.py to convert.

usage: python -I tools/assets/extract.py <what> [--legacy DIR] [--paks DIR] [--jobs 6]
  <what> is a substring of the assets' paths (UI/Widgets, levels/game/act1_1: every asset whose path
  holds it), or a .txt file of package paths, one a line (/Game/models/.../SM_Foo; # starts a
  comment). A package from a list is pulled by its last two path parts (folder/name), which keeps the
  match tight; a name that begins another asset's name pulls that one too, which is harmless.

The legacy folder is research/extract/legacy of this repository unless given; it keeps the game's
layout (Bates/Content/...). Packages already there are not pulled again. retoc is DA_RETOC, else
research/tools/retoc/retoc.exe; it reads the Paks folder (it needs global.utoc beside the
container) and fetches the Oodle library on its first use. The Paks folder is DA_GAME_DIR's, else
Steam's default.
"""

import argparse
import os
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
RETOC = os.environ.get(
    "DA_RETOC", os.path.join(REPO, "research", "tools", "retoc", "retoc.exe")
)
GAME = os.environ.get(
    "DA_GAME_DIR", r"C:\Program Files (x86)\Steam\steamapps\common\Until Dawn"
)
PAKS = os.path.join(GAME, r"Windows\Bates\Content\Paks")
LEGACY = os.path.join(REPO, "research", "extract", "legacy")


def legacy_path(root, package):
    """/Game/a/b/SM_x -> <root>/Bates/Content/a/b/SM_x, /Engine/x -> <root>/Engine/Content/x (no extension)."""
    if package.startswith("/Game/"):
        rel = package.replace("/Game/", "Bates/Content/", 1)
    elif package.startswith("/Engine/"):
        rel = package.replace("/Engine/", "Engine/Content/", 1)
    else:
        rel = package.lstrip("/")
    return os.path.join(root, *rel.split("/"))


def retoc(paks, root, flt):
    return subprocess.run(
        [
            RETOC,
            "to-legacy",
            paks,
            root,
            "-f",
            flt,
            "--no-shaders",
            "--no-script-objects",
        ],
        capture_output=True,
        text=True,
    )


def pull(paks, root, package):
    base = legacy_path(root, package)
    if os.path.exists(base + ".uasset") or os.path.exists(base + ".umap"):
        return "have", package
    result = retoc(paks, root, "/".join(package.split("/")[-2:]))
    if result.returncode != 0:
        return "FAILED (%s)" % (
            result.stderr.strip()[-300:] or result.returncode
        ), package
    if os.path.exists(base + ".uasset") or os.path.exists(base + ".umap"):
        return "ok", package
    return "MISSING after retoc (%s)" % result.stdout.strip()[-120:], package


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("what")
    ap.add_argument("--legacy", default=LEGACY)
    ap.add_argument("--paks", default=PAKS)
    ap.add_argument("--jobs", type=int, default=6)
    args = ap.parse_args()
    if not os.path.exists(RETOC):
        raise SystemExit("retoc not found: %s (set DA_RETOC)" % RETOC)
    os.makedirs(args.legacy, exist_ok=True)
    if not args.what.lower().endswith(".txt"):
        result = retoc(args.paks, args.legacy, args.what)
        print(result.stdout.strip()[-2000:])
        if result.returncode != 0:
            print(result.stderr.strip()[-2000:])
        return result.returncode
    packages = []
    for line in open(args.what, encoding="utf-8"):
        line = line.strip()
        if line and not line.startswith("#"):
            packages.append(line)
    counts = {}
    with ThreadPoolExecutor(max_workers=args.jobs) as pool:
        for status, package in pool.map(
            lambda p: pull(args.paks, args.legacy, p), packages
        ):
            key = status.split(" ")[0]
            counts[key] = counts.get(key, 0) + 1
            if key not in ("ok", "have"):
                print(status, package, flush=True)
    print("retoc:", counts, "of", len(packages))
    return 0 if all(k in ("ok", "have") for k in counts) else 1


if __name__ == "__main__":
    sys.exit(main())
