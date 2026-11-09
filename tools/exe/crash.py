"""Collects the crash dumps of the game and reads them.

usage:
  python -I tools/exe/crash.py collect [--since "YYYY-MM-DD HH:MM"]
  python -I tools/exe/crash.py read <dump.dmp> [<dump.dmp> ...] [--map <main.map>]

Where a crash leaves its dump:
  - UE4SS's crash dumper writes crash_<date>.dmp beside UE4SS.dll, for a fault UE4SS catches
    first: one on the mod's own threads or in UE4SS's event loop (UE4SS-settings.ini, [CrashDump]);
  - the game's crash reporter (Sentry's crashpad, crs-handler.exe) writes
    Binaries/Win64/.crashdata/reports/<id>.dmp, its log in attachments/<id>/crash-report.txt, for a
    fault on the game's threads; the game sends a copy to its makers;
  - Windows Error Reporting, where its local dumps are turned on, writes
    %LOCALAPPDATA%/CrashDumps/Bates-Win64-Shipping.exe.<pid>.dmp.

collect copies every dump not kept yet (and newer than --since) into
research/crashes/<date>-<name>/ with the crash reporter's log, the mod's logs (DawnAccess.log and
DawnAccess.log.old: the game started again after the crash has the crash's log in the .old) and
UE4SS.log as they are at that moment, for a dump of the last two days, and the linker map of the
mod's build the dump names, then reads it into read.txt there. Run it right after a crash, before
the game is started twice more.

read prints the exception (its code and name, the module and function it fell in, for an access
violation what was read or written and where), the faulting thread's instruction pointer, the
stack words of that thread that point into a module, named, from the stack pointer on, their count
per module, and every other thread whose stack holds frames of the mod's modules (main.dll,
UE4SS.dll, prism.dll, dwmapi.dll), the mod's frames named. Stack words are a heuristic: return
addresses and stale values alike, the first ones from the stack pointer the most telling.

The game's code is named by the PDB's function table (research/symbols/functions.tsv, written by
pdb_symbols.py dump; DA_SYMBOLS); the mod's by the linker map of the very build that ran: a map is
taken only where its timestamp is the one the dump records for main.dll. scripts/deploy.ps1 keeps
the map of every build it installs as build/maps/main-<timestamp>.map. Needs the minidump package
(pip install minidump).
"""

import argparse
import bisect
import datetime
import glob
import os
import re
import shutil
import struct
import sys

from minidump.minidumpfile import MinidumpFile

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
SYMBOLS = os.environ.get("DA_SYMBOLS", os.path.join(REPO, "research", "symbols"))
FUNCS = os.path.join(SYMBOLS, "functions.tsv")
KEPT = os.path.join(REPO, "research", "crashes")
MAPS = [os.path.join(REPO, "build", "maps"), os.path.join(REPO, "build", "src")]
GAME = os.environ.get(
    "DA_GAME_DIR", r"C:\Program Files (x86)\Steam\steamapps\common\Until Dawn"
)
WIN64 = os.path.join(GAME, r"Windows\Bates\Binaries\Win64")
OURS = ("main.dll", "ue4ss.dll", "prism.dll", "dwmapi.dll")
MAP_BASE = 0x180000000  # the mod's preferred load address, as its map gives the publics

CODES = {
    0xC0000005: "access violation",
    0xC00000FD: "stack overflow",
    0xC0000409: "fail fast (stack buffer overrun or a deliberate stop)",
    0xC0000374: "heap corruption",
    0xC000001D: "illegal instruction",
    0xC0000094: "integer divide by zero",
    0xC0000420: "assertion failure",
    0xC0000602: "fail fast exception",
    0x80000003: "breakpoint (a debug break or an engine check)",
    0x80000004: "single step",
    0xE06D7363: "C++ exception",
}


def ival(x):
    return getattr(x, "value", x)


def ue4ss_dir():
    sub = os.path.join(WIN64, "ue4ss")
    return sub if os.path.exists(os.path.join(sub, "UE4SS.dll")) else WIN64


# ---- naming ----


class GameNames:
    def __init__(self):
        self.rows = []
        if not os.path.exists(FUNCS):
            print(
                "(no %s: run pdb_symbols.py dump; the game's frames are left unnamed)"
                % FUNCS
            )
            return
        with open(FUNCS, encoding="utf-8", errors="replace") as f:
            next(f)
            for line in f:
                p = line.rstrip("\n").split("\t")
                try:
                    self.rows.append((int(p[0], 16), int(p[1], 16), p[2]))
                except (ValueError, IndexError):
                    pass
        self.rows.sort(key=lambda r: r[0])
        self.starts = [r[0] for r in self.rows]

    def name(self, rva):
        if not self.rows:
            return ""
        i = bisect.bisect_right(self.starts, rva) - 1
        if i >= 0:
            start, length, name = self.rows[i]
            if start <= rva < start + max(length, 1) + 0x40:
                return "%s+0x%x" % (name, rva - start)
        return "?"


def demangle(name):
    """The scopes and name of a plain mangled C++ name (?Name@Class@ns@@...), else the name as is."""
    if not name.startswith("?") or name.startswith("??"):
        return name
    head = name[1:].split("@@", 1)[0]
    parts = ["(anonymous)" if p.startswith("?A0x") else p for p in head.split("@") if p]
    return "::".join(reversed(parts)) if parts else name


def map_timestamp(path):
    with open(path, encoding="utf-8", errors="replace") as f:
        for _ in range(8):
            m = re.search(r"Timestamp is ([0-9a-f]{8})", f.readline())
            if m:
                return int(m.group(1), 16)
    return None


def find_map(stamp, given=None):
    """The linker map of the build whose timestamp the dump records for main.dll."""
    candidates = [given] if given else []
    for folder in MAPS:
        candidates += sorted(glob.glob(os.path.join(folder, "main*.map")))
    for path in candidates:
        if path and os.path.exists(path) and map_timestamp(path) == stamp:
            return path
    return None


class ModNames:
    def __init__(self, path):
        self.syms = []
        in_publics = False
        for line in open(path, encoding="utf-8", errors="replace"):
            if "Publics by Value" in line:
                in_publics = True
                continue
            if in_publics:
                m = re.match(
                    r"\s*([0-9a-f]{4}):([0-9a-f]{8})\s+(\S+)\s+([0-9a-f]{16})",
                    line,
                    re.I,
                )
                if m:
                    self.syms.append((int(m.group(4), 16), m.group(3)))
        self.syms.sort()
        self.starts = [s[0] for s in self.syms]

    def name(self, offset):
        addr = offset + MAP_BASE
        i = bisect.bisect_right(self.starts, addr) - 1
        if i < 0:
            return "?"
        return "%s+0x%x" % (demangle(self.syms[i][1])[:110], addr - self.syms[i][0])


# ---- reading ----


def read(path, given_map=None, out=print):
    md = MinidumpFile.parse(path)
    raw = open(path, "rb").read()
    mods = sorted(
        (m.baseaddress, m.size, m.name.split("\\")[-1], m.timestamp)
        for m in md.modules.modules
    )
    bases = [m[0] for m in mods]

    def module_of(addr):
        i = bisect.bisect_right(bases, addr) - 1
        if i >= 0 and mods[i][0] <= addr < mods[i][0] + mods[i][1]:
            return mods[i][2], addr - mods[i][0]
        return None, 0

    game = GameNames()
    mod_names = None
    main = next((m for m in mods if m[2].lower() == "main.dll"), None)
    out("==== %s" % os.path.basename(path))
    if main:
        found = find_map(main[3], given_map)
        if found:
            mod_names = ModNames(found)
            out(
                "main.dll of build %08x: named by %s"
                % (main[3], os.path.relpath(found, REPO))
            )
        else:
            out(
                "main.dll of build %08x: no map of that build (build/maps); the mod's frames are left unnamed"
                % main[3]
            )
    else:
        out("the mod's main.dll is not loaded in this process")

    def describe(addr):
        name, off = module_of(addr)
        if not name:
            return "0x%x" % addr
        low = name.lower()
        extra = ""
        if low.startswith("bates-win64"):
            extra = " " + game.name(off)
        elif low == "main.dll" and mod_names:
            extra = " " + mod_names.name(off)
        return "%s+0x%x%s" % (name, off, extra)

    if not md.exception or not md.exception.exception_records:
        out("no exception record (a dump taken on request)")
        exc_tid = None
    else:
        exc = md.exception.exception_records[0]
        rec = exc.ExceptionRecord
        # The package names known codes and keeps every code as read in ExceptionCode_raw (an
        # unknown one, heap corruption among them, is EXCEPTION_UNKNOWN otherwise).
        code = getattr(rec, "ExceptionCode_raw", None)
        if not isinstance(code, int):
            code = ival(rec.ExceptionCode)
            code = code if isinstance(code, int) else 0
        exc_tid = exc.ThreadId
        info = [ival(v) for v in rec.ExceptionInformation[: rec.NumberParameters]]
        what = ""
        if code == 0xC0000005 and len(info) >= 2:
            kind = {0: "reading", 1: "writing", 8: "executing"}.get(info[0], "touching")
            what = "; %s 0x%x" % (kind, info[1])
        out(
            "exception 0x%08x %s at %s on thread %d%s"
            % (
                code,
                CODES.get(code, ""),
                describe(ival(rec.ExceptionAddress)),
                exc_tid,
                what,
            )
        )

    def context_of(t):
        return raw[t.ThreadContext.Rva : t.ThreadContext.Rva + t.ThreadContext.DataSize]

    def stack_words(t):
        ctx = context_of(t)
        rsp = struct.unpack_from("<Q", ctx, 0x98)[0] if len(ctx) >= 0x100 else 0
        start = t.Stack.StartOfMemoryRange
        loc = t.Stack.MemoryLocation
        data = raw[loc.Rva : loc.Rva + loc.DataSize]
        first = (rsp - start) // 8 * 8 if start <= rsp < start + len(data) else 0
        for i in range(first, len(data) - 7, 8):
            yield i - first, struct.unpack_from("<Q", data, i)[0]

    threads = md.threads.threads if md.threads else []
    faulting = next((t for t in threads if t.ThreadId == exc_tid), None)
    if faulting:
        ctx = context_of(faulting)
        if len(ctx) >= 0x100:
            rip = struct.unpack_from("<Q", ctx, 0xF8)[0]
            rsp = struct.unpack_from("<Q", ctx, 0x98)[0]
            out("rip %s; rsp 0x%x" % (describe(rip), rsp))
        out(
            "stack words of thread %d that point into a module, from the stack pointer:"
            % exc_tid
        )
        counts = {}
        shown = 0
        for offset, value in stack_words(faulting):
            name, _ = module_of(value)
            if not name:
                continue
            counts[name] = counts.get(name, 0) + 1
            if shown < 60:
                out("  +%05x %s" % (offset, describe(value)))
                shown += 1
        out("per module: %s" % dict(sorted(counts.items(), key=lambda kv: -kv[1])))
    for t in threads:
        if t is faulting:
            continue
        frames = []
        for offset, value in stack_words(t):
            name, _ = module_of(value)
            if name and name.lower() in OURS:
                frames.append("  +%05x %s" % (offset, describe(value)))
        if frames:
            ctx = context_of(t)
            rip = struct.unpack_from("<Q", ctx, 0xF8)[0] if len(ctx) >= 0x100 else 0
            out(
                "thread %d (rip %s) holds %d frames of the mod's modules:"
                % (t.ThreadId, describe(rip), len(frames))
            )
            for line in frames[:16]:
                out(line)


# ---- collecting ----


def dumps_now():
    found = []
    found += glob.glob(os.path.join(ue4ss_dir(), "crash_*.dmp"))
    found += glob.glob(os.path.join(WIN64, ".crashdata", "reports", "*.dmp"))
    local = os.environ.get("LOCALAPPDATA")
    if local:
        found += glob.glob(
            os.path.join(local, "CrashDumps", "Bates-Win64-Shipping.exe*.dmp")
        )
    return sorted(found, key=os.path.getmtime)


def collect(since):
    os.makedirs(KEPT, exist_ok=True)
    kept_names = {name.split("-", 2)[-1] for name in os.listdir(KEPT)}
    taken = 0
    for path in dumps_now():
        when = datetime.datetime.fromtimestamp(os.path.getmtime(path))
        stem = os.path.splitext(os.path.basename(path))[0]
        if since and when < since:
            continue
        if stem in kept_names:
            continue
        folder = os.path.join(KEPT, "%s-%s" % (when.strftime("%Y%m%d-%H%M%S"), stem))
        os.makedirs(folder)
        shutil.copy2(path, folder)
        report = os.path.join(
            WIN64, ".crashdata", "attachments", stem, "crash-report.txt"
        )
        logs = [report] if os.path.exists(report) else []
        # The logs of the launch that crashed survive one launch more (DawnAccess.log.old); a dump
        # older than two days has none left, and today's logs beside it would mislead.
        recent = datetime.datetime.now() - when < datetime.timedelta(days=2)
        mod = os.path.join(ue4ss_dir(), "Mods", "DawnAccess")
        if recent:
            logs += [
                os.path.join(mod, "DawnAccess.log"),
                os.path.join(mod, "DawnAccess.log.old"),
                os.path.join(ue4ss_dir(), "UE4SS.log"),
            ]
        for log in logs:
            if os.path.exists(log):
                shutil.copy2(log, folder)
        dump = os.path.join(folder, os.path.basename(path))
        md = MinidumpFile.parse(dump)
        main = next(
            (m for m in md.modules.modules if m.name.lower().endswith("\\main.dll")),
            None,
        )
        if main:
            found = find_map(main.timestamp)
            if found:
                shutil.copy2(
                    found, os.path.join(folder, "main-%08x.map" % main.timestamp)
                )
        lines = []
        read(dump, out=lines.append)
        with open(os.path.join(folder, "read.txt"), "w", encoding="utf-8") as f:
            f.write(
                "collected %s from %s\n"
                % (datetime.datetime.now().strftime("%Y-%m-%d %H:%M:%S"), path)
            )
            if not recent:
                f.write(
                    "the mod's logs are not kept: the dump is older than the logs left\n"
                )
            f.write("\n".join(lines) + "\n")
        print(
            "kept %s (written %s) in %s"
            % (path, when.strftime("%Y-%m-%d %H:%M:%S"), os.path.relpath(folder, REPO))
        )
        print("\n".join(lines))
        taken += 1
    if not taken:
        print("no new crash dump")


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    sub = parser.add_subparsers(dest="command", required=True)
    p = sub.add_parser("collect")
    p.add_argument("--since", help='"YYYY-MM-DD" or "YYYY-MM-DD HH:MM"')
    p = sub.add_parser("read")
    p.add_argument("dumps", nargs="+")
    p.add_argument("--map", help="the linker map of the mod's build that ran")
    args = parser.parse_args()
    if args.command == "collect":
        since = None
        if args.since:
            fmt = "%Y-%m-%d %H:%M" if " " in args.since else "%Y-%m-%d"
            since = datetime.datetime.strptime(args.since, fmt)
        collect(since)
    else:
        for path in args.dumps:
            read(path, args.map)
    return 0


if __name__ == "__main__":
    sys.exit(main())
