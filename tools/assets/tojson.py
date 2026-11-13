r"""Converts legacy .uasset/.umap files (extract.py) to JSON with UAssetGUI and the game's mappings.

usage: python -I tools/assets/tojson.py <legacy root> <json root> [--list FILE] [--filter TEXT]
         [--mappings Bates|BatesPlay] [--jobs 4] [--force]
  every asset under the legacy root is converted, or those a --list file names (one path a line),
  or those whose path holds the --filter text; the JSON keeps the legacy layout under the json root.
  JSON files newer than their asset are kept unless --force is given.

The mappings (.usmap) tell UAssetGUI the layouts of the game's types, which cooked assets do not
carry. UAssetGUI applies them only by a name registered in %LOCALAPPDATA%\UAssetGUI\Mappings
(<name>.usmap); a path on its command line is silently not applied, and every export comes out raw.
  Bates      the mappings dumped at the main menu (research/dumps/Bates-mainmenu-v3.usmap): the
             native classes, enough for widgets, data assets and inputs;
  BatesPlay  the mappings dumped in play (research/dumps/Bates-play.usmap): the Blueprint classes
             of levels' actors too (trigger volumes, doors), so that they come out as properties.
A known name not registered yet is registered from DA_USMAPS, else research/dumps (git-ignored: the
mappings are the game's own types, made on each machine). A new dump is made in the game by USMapDump
(beside this script; a UE4SS Lua mod, enabled for that one launch only) and saved under the name above.

UAssetGUI is DA_UASSETGUI, else research/tools/UAssetGUI.exe. It is only ever run with the tojson
arguments: run without them it opens a window.
"""

import argparse
import os
import shutil
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
UASSETGUI = os.environ.get(
    "DA_UASSETGUI", os.path.join(REPO, "research", "tools", "UAssetGUI.exe")
)
ENGINE = "VER_UE5_3"
MAPPINGS_DIR = os.path.join(os.environ.get("LOCALAPPDATA", ""), "UAssetGUI", "Mappings")
USMAPS = os.environ.get("DA_USMAPS", os.path.join(REPO, "research", "dumps"))
KNOWN_USMAPS = {
    "Bates": os.path.join(USMAPS, "Bates-mainmenu-v3.usmap"),
    "BatesPlay": os.path.join(USMAPS, "Bates-play.usmap"),
}


def ensure_registered(name):
    """Copies a known usmap into UAssetGUI's Mappings folder when it is not there yet."""
    target = os.path.join(MAPPINGS_DIR, name + ".usmap")
    if os.path.exists(target):
        return True
    source = KNOWN_USMAPS.get(name)
    if not source or not os.path.exists(source):
        print(
            "mappings %r are not registered in %s, and %s is missing: dump them in the game with"
            " USMapDump and save them there, or set DA_USMAPS to the folder that holds them"
            % (name, MAPPINGS_DIR, source or name + ".usmap")
        )
        return False
    os.makedirs(MAPPINGS_DIR, exist_ok=True)
    shutil.copyfile(source, target)
    print("registered", name, "from", source)
    return True


def convert(src, dst, mappings, force):
    os.makedirs(os.path.dirname(dst), exist_ok=True)
    if (
        not force
        and os.path.exists(dst)
        and os.path.getmtime(dst) >= os.path.getmtime(src)
    ):
        return "kept", src
    result = subprocess.run(
        [UASSETGUI, "tojson", src, dst, ENGINE, mappings],
        capture_output=True,
        text=True,
    )
    if result.returncode != 0 or not os.path.exists(dst):
        return "FAILED (%s)" % (
            result.stderr.strip() or result.stdout.strip() or result.returncode
        ), src
    return "ok", src


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("legacy")
    ap.add_argument("json")
    ap.add_argument("--list")
    ap.add_argument("--filter", default="")
    ap.add_argument("--mappings", default="Bates")
    ap.add_argument("--jobs", type=int, default=4)
    ap.add_argument("--force", action="store_true")
    args = ap.parse_args()
    if not os.path.exists(UASSETGUI):
        raise SystemExit("UAssetGUI not found: %s (set DA_UASSETGUI)" % UASSETGUI)
    if not ensure_registered(args.mappings):
        return 2
    if args.list:
        sources = [
            line.strip() for line in open(args.list, encoding="utf-8") if line.strip()
        ]
    else:
        sources = []
        for root, _, files in os.walk(args.legacy):
            for name in files:
                if name.lower().endswith((".uasset", ".umap")):
                    sources.append(os.path.join(root, name))
    pairs = []
    for src in sources:
        if args.filter and args.filter.lower() not in src.lower():
            continue
        rel = os.path.relpath(src, args.legacy)
        pairs.append((src, os.path.join(args.json, os.path.splitext(rel)[0] + ".json")))
    counts = {}
    with ThreadPoolExecutor(max_workers=args.jobs) as pool:
        for status, src in pool.map(
            lambda p: convert(p[0], p[1], args.mappings, args.force), pairs
        ):
            key = status.split(" ")[0]
            counts[key] = counts.get(key, 0) + 1
            if key == "FAILED":
                print(status, src, flush=True)
    print("done:", counts, "of", len(pairs))
    return 0 if "FAILED" not in counts else 1


if __name__ == "__main__":
    sys.exit(main())
