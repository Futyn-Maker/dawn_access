"""Collects the licences of the libraries UE4SS.dll is built from into third_party/ue4ss-licences, for
the release to ship beside UE4SS's own (scripts/common.ps1 Get-DaInstallFiles).

usage: python -I tools/release/collect_licences.py

UE4SS.dll links fmt, glaze, ImGui (with ImGuiColorTextEdit), PolyHook 2 (with asmjit, asmtk, Zydis and
Zycore), GLFW, glad and the patternsleuth crate with the Rust crates it pulls in, and carries the Droid
and Roboto fonts (RE-UE4SS/UE4SS/CMakeLists.txt, the target's link libraries). The C++ libraries'
licences are read from the build's fetched sources (build/_deps, after scripts/build.cmd), the fonts' and
the generator's from the RE-UE4SS tree, the crates' from Cargo's registry (CARGO_HOME, else
%USERPROFILE%/.cargo) by the versions patternsleuth_bind's Cargo.lock pins: Cargo fetches only the crates
the build's target compiles, so a crate the lock lists for another target is not in the binary and not
named. Every file found is copied under the library's name; the crates get one file, CRATES.txt, naming
each crate, its version, its licence and the licence texts that follow it. A library whose licence is
not found is named at the end and the run fails, so that the release never goes with one missing.
"""

import glob
import os
import re
import shutil
import sys

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
DEPS = os.path.join(REPO, "build", "_deps")
UE4SS = os.path.join(REPO, "third_party", "RE-UE4SS")
OUT = os.path.join(REPO, "third_party", "ue4ss-licences")
CARGO = os.environ.get("CARGO_HOME", os.path.join(os.path.expanduser("~"), ".cargo"))

# name, the files that hold its licence (globs, the first that exists is taken)
LIBRARIES = [
    ("fmt", [os.path.join(DEPS, "fmt-src", "LICENSE*")]),
    ("glaze", [os.path.join(DEPS, "glaze-src", "LICENSE*")]),
    ("imgui", [os.path.join(DEPS, "imgui-src", "LICENSE*")]),
    ("ImGuiColorTextEdit", [os.path.join(DEPS, "imguitextedit-src", "LICENSE*")]),
    ("PolyHook_2", [os.path.join(DEPS, "polyhook2-src", "LICENSE*")]),
    ("asmjit", [os.path.join(DEPS, "polyhook2-src", "asmjit", "LICENSE*")]),
    ("asmtk", [os.path.join(DEPS, "polyhook2-src", "asmtk", "LICENSE*")]),
    ("zydis", [os.path.join(DEPS, "zydis-src", "LICENSE*")]),
    ("zycore", [os.path.join(DEPS, "zydis-src", "dependencies", "zycore", "LICENSE*")]),
    ("glfw", [os.path.join(DEPS, "glfw-src", "LICENSE*")]),
    (
        "glad",
        [
            os.path.join(UE4SS, "deps", "third", "glad", "LICENSE*"),
            os.path.join(UE4SS, "deps", "third", "glad", "include", "glad", "*.h"),
        ],
    ),
    (
        "Droid_font",
        [os.path.join(UE4SS, "UE4SS", "include", "fonts", "LICENSE_Droid.txt")],
    ),
    (
        "Roboto_font",
        [os.path.join(UE4SS, "UE4SS", "include", "fonts", "LICENSE_Roboto.txt")],
    ),
    (
        "USMapGenerator",
        [os.path.join(UE4SS, "UE4SS", "src", "USMapGenerator", "LICENSE*")],
    ),
]

# A library whose source holds no licence file, only the licence its manifest names: said so, with
# where its source is.
NAMED = [
    (
        "patternsleuth",
        "MIT OR Apache-2.0, as its Cargo.toml names it; its source: https://github.com/trumank/patternsleuth",
    ),
]


def first(pattern):
    found = sorted(glob.glob(pattern))
    return found[0] if found else None


def glad_notice(header):
    """glad's generated header carries its licence notice at its top: the comment block before the code."""
    text = open(header, encoding="utf-8", errors="replace").read()
    m = re.match(r"\s*/\*(.*?)\*/", text, re.S)
    return m.group(1).strip() if m else None


def crates():
    lock = os.path.join(UE4SS, "deps", "first", "patternsleuth_bind", "Cargo.lock")
    text = open(lock, encoding="utf-8").read()
    out = []
    for block in text.split("[[package]]")[1:]:
        name = re.search(r'name = "([^"]+)"', block).group(1)
        version = re.search(r'version = "([^"]+)"', block).group(1)
        source = re.search(r'source = "([^"]+)"', block)
        out.append((name, version, source.group(1) if source else None))
    return out


def crate_dir(name, version):
    for root in glob.glob(os.path.join(CARGO, "registry", "src", "*")):
        path = os.path.join(root, "%s-%s" % (name, version))
        if os.path.isdir(path):
            return path
    return None


def copy_library(name, patterns):
    for pattern in patterns:
        path = first(pattern)
        if not path:
            continue
        target = os.path.join(OUT, name + ".txt")
        if path.endswith(".h"):
            notice = glad_notice(path)
            if not notice:
                continue
            open(target, "w", encoding="utf-8", newline="\n").write(notice + "\n")
        else:
            shutil.copyfile(path, target)
        return True
    return False


def crate_lines(missing):
    lines = [
        "The Rust crates patternsleuth is built with (patternsleuth_bind's Cargo.lock), with their licences.",
        "Crates the lock lists for other targets than Windows on x64 are not built and not named.",
        "",
    ]
    for name, version, source in crates():
        if source is None:
            continue  # the workspace's own crates: patternsleuth and its binding
        folder = crate_dir(name, version)
        if not folder:
            continue  # not fetched: not compiled for this target
        toml = open(
            os.path.join(folder, "Cargo.toml"), encoding="utf-8", errors="replace"
        ).read()
        licence = re.search(r'^license\s*=\s*"([^"]+)"', toml, re.M)
        texts = sorted(
            glob.glob(os.path.join(folder, "LICENSE*"))
            + glob.glob(os.path.join(folder, "COPYING*"))
        )
        if not texts and not licence:
            missing.append("crate %s %s (no licence)" % (name, version))
        lines.append("=" * 78)
        lines.append(
            "%s %s: %s" % (name, version, licence.group(1) if licence else "see below")
        )
        for path in texts:
            lines.append("-" * 78)
            lines.append(os.path.basename(path))
            lines.append(open(path, encoding="utf-8", errors="replace").read().strip())
        lines.append("")
    return lines


def main():
    if os.path.isdir(OUT):
        shutil.rmtree(OUT)
    os.makedirs(OUT)
    missing = [name for name, patterns in LIBRARIES if not copy_library(name, patterns)]
    for name, words in NAMED:
        open(
            os.path.join(OUT, name + ".txt"), "w", encoding="utf-8", newline="\n"
        ).write(name + " is licensed " + words + ".\n")
    lines = crate_lines(missing)
    open(os.path.join(OUT, "CRATES.txt"), "w", encoding="utf-8", newline="\n").write(
        "\n".join(lines) + "\n"
    )
    print("licences in", OUT, "-", len(os.listdir(OUT)), "files")
    if missing:
        print("missing:", ", ".join(missing))
        sys.exit(1)


if __name__ == "__main__":
    if len(sys.argv) > 1 and sys.argv[1] in ("-h", "--help"):
        print(__doc__)
        sys.exit(0)
    main()
