"""Finds the places and members of core/Symbols' reference table in another build of the game's
executable, for a build that ships no program database (.claude/rules/core.md, "A new build").

usage:
  python -I tools/exe/port_build.py places <other exe>
      every place of the reference table (kSteamPlaces in src/core/Symbols.cpp) in the other build.
      A function is matched by its code with its relative branch targets, RIP-relative displacements
      and in-image 64-bit immediates masked: "exact" when the masked code is this function's alone in
      the reference, starts exactly one function in the other build, and names the same strings.
      Displacements relative to the image's base (jump tables) are not masked, so such a function
      finds no exact match. Otherwise the best candidates at the shift of its exactly-matched
      neighbours are listed with their likeness ("near"): read them with "side" before taking one. A
      place of data is read from the instructions that use it in the reference's functions, at the
      same instruction of their exact matches, with the number of functions that agree: a single
      one (a stub's pointer has only its stub) is checked with "side".
  python -I tools/exe/port_build.py members <other exe>
      every member of the reference table (kMembers) checked by the reference's functions that use its
      offset off an object's register (the stack and the frame pointer left out): those of its own
      class first, then of the classes MEMBER_FUNCTIONS names. "exact" when such a function matches
      exactly, "near" when a near match still uses the offset, UNPROVEN otherwise; the instruction is
      printed. An exact match proves the member only where that instruction touches the member
      itself: check a small offset, and another class's function, by reading the instruction.
  python -I tools/exe/port_build.py layout <other exe>
      the reflected property records of both builds (UHT's property parameters: the name's pointer
      first, the array dimension at +0x30 and the offset at +0x32) compared by name and offset: a
      class whose layout moved shows its properties at other offsets. Bool properties carry no offset
      in their record and are not compared. Records found in one build only are listed by name: a
      property added at the end of a class's reflected block shows there, not among the moved ones.
  python -I tools/exe/port_build.py side <reference function or rva> <other rva>
      the two functions' instructions side by side, with the targets of their RIP-relative operands.
  python -I tools/exe/port_build.py callee <reference caller> <reference callee> <other exe>
      the callee's address in the other build, read from the call or jump of the caller's exact
      match: the way to a function whose own code changed but whose caller did not.

The reference is Steam's executable (DA_GAME_DIR, else Steam's default folder) with its database's
function table, research/symbols/functions.tsv (DA_SYMBOLS; pdb_symbols.py dump writes it). "side"
takes the other executable from DA_OTHER_EXE.
"""

import bisect
import collections
import os
import re
import struct
import sys

import capstone
import numpy as np
import pefile

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
SYMBOLS = os.environ.get("DA_SYMBOLS", os.path.join(REPO, "research", "symbols"))
FUNCS = os.path.join(SYMBOLS, "functions.tsv")
GAME = os.environ.get(
    "DA_GAME_DIR", r"C:\Program Files (x86)\Steam\steamapps\common\Until Dawn"
)
REFERENCE = os.path.join(GAME, r"Windows\Bates\Binaries\Win64\Bates-Win64-Shipping.exe")
SYMBOLS_CPP = os.path.join(REPO, "src", "core", "Symbols.cpp")

X86 = capstone.x86

# Members whose struct has no functions of its own name: the functions that touch them.
MEMBER_FUNCTIONS = {
    "UBMTTSQueueItem": ["UBMTTSSubsystem::"],
    "FStairActivityCharacterData": ["ABatesStairActivityActor::"],
    "FProcessedActionMapping": ["UBMUIActionMappingCache::"],
    "FKeyWithChord": ["UBMUIActionMappingCache::", "UBMUIActionIcons::"],
    "UScript": ["ULuaScriptUpdater::", "UBatesLuaScriptUpdater::"],
    "UAnimBlueprintGeneratedClass": ["UAnimInstance::", "FAnimInstanceProxy::"],
    "UBatesCreditQueueObjectBase": [
        "UBatesCreditsManager::",
        "UBatesGameScriptInterface::",
    ],
    "UBatesCreditMovieQueueObject": [
        "UBatesCreditsManager::",
        "UBatesGameScriptInterface::",
    ],
    "UBatesBindableCharacterComponent": ["ABatesUseLocation::"],
    "ABatesUseLocation": ["ABatesGlint::"],
}


class Image:
    """An executable's sections, read once."""

    def __init__(self, path):
        if not os.path.exists(path):
            raise SystemExit("%s is missing" % path)
        self.pe = pefile.PE(path, fast_load=True)
        self.base = self.pe.OPTIONAL_HEADER.ImageBase
        self.size = self.pe.OPTIONAL_HEADER.SizeOfImage
        self.sections = []
        for s in self.pe.sections:
            name = s.Name.rstrip(b"\0").decode()
            self.sections.append(
                (
                    name,
                    s.VirtualAddress,
                    max(s.Misc_VirtualSize, s.SizeOfRawData),
                    s.get_data(),
                )
            )
        self.text = next(s for s in self.sections if s[0] == ".text")

    def section(self, name):
        return next(s for s in self.sections if s[0] == name)

    def read(self, rva, n):
        for _, start, size, data in self.sections:
            if start <= rva < start + size:
                return data[rva - start : rva - start + n]
        return b""


def disasm(image, rva, size):
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
    md.detail = True
    return list(md.disasm(image.read(rva, size), rva))


def body(image, rva, limit=0x1000):
    """A function's instructions from rva to the int3 padding (or a ret before it) that ends it."""
    out = []
    for insn in disasm(image, rva, limit):
        out.append(insn)
        if insn.mnemonic == "int3" or (
            insn.mnemonic == "ret"
            and image.read(insn.address + insn.size, 1) == b"\xcc"
        ):
            break
    return out


def is_rip(insn):
    return any(
        op.type == X86.X86_OP_MEM and op.mem.base == X86.X86_REG_RIP
        for op in insn.operands
    )


def is_branch(insn):
    return insn.group(capstone.CS_GRP_JUMP) or insn.group(capstone.CS_GRP_CALL)


def masked(image, insns):
    """The instructions' bytes with every address-dependent byte masked: relative branch targets,
    RIP-relative displacements and 64-bit immediates inside the image."""
    data = bytearray()
    keep = []
    for insn in insns:
        b = bytes(insn.bytes)
        k = [True] * len(b)
        if is_branch(insn) and insn.imm_size:
            k[insn.imm_offset : insn.imm_offset + insn.imm_size] = [
                False
            ] * insn.imm_size
        if insn.disp_size and is_rip(insn):
            k[insn.disp_offset : insn.disp_offset + insn.disp_size] = [
                False
            ] * insn.disp_size
        if insn.imm_size == 8 and not is_branch(insn):
            (imm,) = struct.unpack_from("<Q", b, insn.imm_offset)
            if image.base <= imm < image.base + image.size:
                k[insn.imm_offset : insn.imm_offset + 8] = [False] * 8
        data += b
        keep += k
    return bytes(data), keep


def search(target, data, keep, limit=3):
    """Every RVA of the target's code where the masked bytes match, the longest literal run first."""
    _, base, _, text = target.text
    parts, run, best, start = [], bytearray(), (0, 0), None
    for i, (byte, k) in enumerate(zip(data, keep)):
        if k:
            run.append(byte)
            start = i if start is None else start
            if i + 1 - start > best[1] - best[0]:
                best = (start, i + 1)
        else:
            if run:
                parts.append(re.escape(bytes(run)))
                run = bytearray()
            parts.append(b".")
            start = None
    if run:
        parts.append(re.escape(bytes(run)))
    rx = re.compile(b"".join(parts), re.S)
    hits = []
    a, b = best
    if b - a >= 6:
        i = text.find(data[a:b])
        while i != -1 and len(hits) < limit:
            if i - a >= 0 and rx.match(text, i - a):
                hits.append(base + i - a)
            i = text.find(data[a:b], i + 1)
    else:
        for m in rx.finditer(text):
            hits.append(base + m.start())
            if len(hits) >= limit:
                break
    return sorted(set(hits))


def rip_target(insn):
    (disp,) = struct.unpack_from("<i", bytes(insn.bytes), insn.disp_offset)
    return insn.address + insn.size + disp


def features(insns):
    """The instructions' mnemonics with their displacements and immediates, registers left out."""
    out = collections.Counter()
    for insn in insns:
        key = [insn.mnemonic]
        for op in insn.operands:
            if op.type == X86.X86_OP_MEM:
                key.append(
                    "rip"
                    if op.mem.base == X86.X86_REG_RIP
                    else "m%x/%d" % (op.mem.disp & 0xFFFFFFFF, op.size)
                )
            elif op.type == X86.X86_OP_IMM:
                key.append(
                    "t" if is_branch(insn) else "i%x" % (op.imm & 0xFFFFFFFFFFFFFFFF)
                )
        out[" ".join(key)] += 1
    return out


def likeness(a, b):
    union = sum((a | b).values())
    return sum((a & b).values()) / union if union else 0.0


def function_starts(image, lo, hi):
    """16-aligned addresses in [lo, hi) after int3 padding, a ret or nop padding."""
    _, base, _, data = image.text
    out = []
    for rva in range(lo & ~0xF, hi, 0x10):
        off = rva - base
        if 2 <= off < len(data) and (
            data[off - 1] in (0xCC, 0xC3) or data[off - 2 : off] == b"\x90\x90"
        ):
            out.append(rva)
    return out


class Builds:
    def __init__(self, other):
        self.ref = Image(REFERENCE)
        self.new = Image(other)
        rows = []
        with open(FUNCS, encoding="utf-8", errors="replace") as f:
            for line in f:
                p = line.rstrip("\n").split("\t")
                try:
                    rows.append((int(p[0], 16), int(p[1], 16), p[2]))
                except (ValueError, IndexError):
                    pass
        self.rows = sorted(set(r for r in rows if r[1] > 0))
        self.starts = [r for r, _, _ in self.rows]
        self.by_name = {}
        self.at = {}
        for r, s, n in self.rows:
            self.by_name.setdefault(n, (r, s))
            self.at.setdefault(r, (s, n))
        # The reference's unwind table gives the extent of a function the database records without
        # one (a public symbol only, like the pad library's stubs).
        self.unwind = unwind_table(self.ref)
        self.unwind_starts = [b for b, _ in self.unwind]
        self.new_begins = {b for b, _ in unwind_table(self.new)}

    def extent(self, rva):
        """The size of the reference function that begins at rva, or 0."""
        if rva in self.at:
            return self.at[rva][0]
        i = bisect.bisect_left(self.unwind_starts, rva)
        if i < len(self.unwind) and self.unwind[i][0] == rva:
            return self.unwind[i][1] - rva
        return 0

    def owner(self, rva):
        """(start, size, name) of the reference function that holds rva."""
        i = bisect.bisect_right(self.starts, rva) - 1
        while i >= 0 and self.rows[i][0] > rva - 0x4000:
            r, s, n = self.rows[i]
            if r <= rva < r + s:
                return r, s, n
            i -= 1
        i = bisect.bisect_right(self.unwind_starts, rva) - 1
        if i >= 0 and self.unwind[i][0] <= rva < self.unwind[i][1]:
            b, e = self.unwind[i]
            return b, e - b, "sub_%x" % b
        return None

    def starts_function(self, rva):
        """Whether a function of the other build begins at rva: its unwind table's, or a leaf's
        16-aligned start after padding."""
        if rva in self.new_begins:
            return True
        _, base, _, data = self.new.text
        off = rva - base
        return rva % 16 == 0 and 2 <= off < len(data) and (data[off - 1] in (0xCC, 0xC3) or data[off - 2 : off] == b"\x90\x90")

    def exact(self, rva, size):
        """The other build's one function whose code is this one's with the address-dependent bytes
        masked, which begins a function there and names the same strings; None unless the masked
        code is also this function's alone in the reference (two functions told apart only by a
        masked call cannot be told apart in the other build either)."""
        insns = disasm(self.ref, rva, size)
        if not insns:
            return None, insns
        data, keep = masked(self.ref, insns)
        alike = [h for h in search(self.ref, data, keep, limit=8) if same_strings(self.ref, insns, self.ref, disasm(self.ref, h, size))]
        if alike != [rva]:
            return None, insns
        found = search(self.new, data, keep, limit=64)
        if len(found) >= 64:
            return None, insns
        hits = [h for h in found if self.starts_function(h) and same_strings(self.ref, insns, self.new, disasm(self.new, h, size))]
        return (hits[0] if len(hits) == 1 else None), insns

    def local_shift(self, rva, around=40):
        i = bisect.bisect_left(self.starts, rva)
        found = []
        for r, s, _ in self.rows[max(0, i - around) : i + around]:
            if s < 0x20:
                continue
            hit, _ = self.exact(r, s)
            if hit is not None:
                found.append((abs(r - rva), hit - r))
        found.sort()
        return [d for _, d in found[:6]]

    def find(self, rva, size):
        """(likeness, other rva, how) of the best candidates for a reference function."""
        hit, insns = self.exact(rva, size)
        if hit is not None:
            return [(1.0, hit, "exact")]
        shifts = self.local_shift(rva)
        if not shifts or not insns:
            return []
        want = features(insns)
        scored = []
        for start in function_starts(
            self.new, rva + min(shifts) - 0x800, rva + max(shifts) + 0x800
        ):
            scored.append(
                (
                    likeness(
                        want, features(body(self.new, start, max(size * 2, 0x40)))
                    ),
                    start,
                    "near",
                )
            )
        return sorted(scored, reverse=True)[:3]


def reference_table():
    text = open(SYMBOLS_CPP, encoding="utf-8").read()

    def block(name):
        b = text[text.index("constexpr %s[] = {" % name) :]
        return b[: b.index("};")]

    entry = re.compile(r'\{L"([^"]+)",\s*0x([0-9a-fA-F]+)\}')
    places = [
        (m.group(1), int(m.group(2), 16))
        for m in entry.finditer(block("Place kSteamPlaces"))
    ]
    members = [
        (m.group(1), int(m.group(2), 16))
        for m in entry.finditer(block("Member kMembers"))
    ]
    return places, members


def string_at(image, rva):
    """The NUL-terminated string at rva, as ASCII or UTF-16, or None where the bytes are no string."""
    raw = image.read(rva, 256)
    end = raw.find(b"\0")
    if end >= 3 and all(32 <= c < 127 for c in raw[:end]):
        return raw[:end].decode()
    wide = raw.decode("utf-16-le", "replace").split("\0", 1)[0] if len(raw) >= 8 else ""
    if len(wide) >= 3 and all(32 <= ord(c) < 127 for c in wide):
        return wide
    return None


def same_strings(a_image, a_insns, b_image, b_insns):
    """Whether the two functions' RIP-relative operands that point at a string point at the same
    string (the pad library's stubs differ only in the call they name). Other read-only data, the
    import slots and tables of pointers, holds addresses that differ by build."""
    for a, b in zip(a_insns, b_insns):
        if a.disp_size and is_rip(a) and b.disp_size and is_rip(b):
            s = string_at(a_image, rip_target(a))
            if s is not None and s != string_at(b_image, rip_target(b)):
                return False
    return True


def unwind_table(image):
    """(begin, end) of every function the executable's exception directory lists, sorted."""
    directory = image.pe.OPTIONAL_HEADER.DATA_DIRECTORY[
        pefile.DIRECTORY_ENTRY["IMAGE_DIRECTORY_ENTRY_EXCEPTION"]
    ]
    raw = image.read(directory.VirtualAddress, directory.Size)
    entries = np.frombuffer(raw[: len(raw) // 12 * 12], dtype="<u4").reshape(-1, 3)
    return sorted({(int(b), int(e)) for b, e, _ in entries if b and e > b})


def references(image, target):
    """Every position in the code whose 32-bit field, read as a RIP-relative displacement of an
    instruction ending 0, 1, 2 or 4 bytes after it, points at the target."""
    _, base, _, text = image.text
    data = np.frombuffer(text, dtype=np.uint8)
    out = []
    chunk = 1 << 23
    for lo in range(0, len(data) - 4, chunk):
        hi = min(lo + chunk, len(data) - 4)
        d = (
            data[lo:hi].astype(np.int64)
            | (data[lo + 1 : hi + 1].astype(np.int64) << 8)
            | (data[lo + 2 : hi + 2].astype(np.int64) << 16)
            | (data[lo + 3 : hi + 3].astype(np.int64) << 24)
        )
        d = np.where(d >= 2**31, d - 2**32, d)
        pos = np.arange(lo, hi, dtype=np.int64)
        for tail in (0, 1, 2, 4):
            out.extend(
                (base + p)
                for p in np.nonzero(base + pos + 4 + tail + d == target)[0] + lo
            )
    return sorted(set(int(p) for p in out))


def cmd_places(b):
    places, _ = reference_table()
    text = b.ref.text
    for name, rva in places:
        size = b.extent(rva)
        if text[1] <= rva < text[1] + text[2] and size:
            fname = b.at[rva][1] if rva in b.at else "sub_%x" % rva
            res = b.find(rva, size)
            if res and res[0][2] == "exact":
                print("%s\t0x%x\texact\t%s" % (name, res[0][1], fname))
            else:
                print(
                    "%s\t-\tnear\t%s\t%s"
                    % (name, fname, ", ".join("0x%x %.2f" % (a, s) for s, a, _ in res))
                )
            continue
        # Data: the instructions that use it, in the reference's functions that match.
        seen = collections.Counter()
        tried = 0
        for at in references(b.ref, rva):
            own = b.owner(at)
            if not own or own[1] > 0x2000:
                continue
            r, s, fname = own
            insns = disasm(b.ref, r, s)
            idx = [
                i
                for i, insn in enumerate(insns)
                if insn.disp_size and is_rip(insn) and rip_target(insn) == rva
            ]
            if not idx:
                continue
            hit, _ = b.exact(r, s)
            tried += 1
            if hit is None:
                continue
            mine = disasm(b.new, hit, s)
            for i in idx:
                if i < len(mine) and mine[i].disp_size and is_rip(mine[i]):
                    seen[(rip_target(mine[i]), fname)] += 1
            if len(seen) >= 3 or tried >= 12:
                break
        targets = collections.Counter(t for t, _ in seen.elements())
        if targets:
            best, count = targets.most_common(1)[0]
            by = sorted({f for (t, f) in seen if t == best})
            if len(targets) > 1:
                agree = "OTHERS: " + ", ".join("0x%x" % t for t in targets if t != best)
            elif len(by) >= 2:
                agree = "%d functions agree" % len(by)
            else:
                agree = "one function only: check it with side"
            print("%s\t0x%x\tdata\tby %s (%s)" % (name, best, ", ".join(by[:3]), agree))
        else:
            print("%s\t-\tdata\tUNFOUND: no function that uses it matched (%d tried)" % (name, tried))
        sys.stdout.flush()


def uses(insns, disp):
    """(mnemonic, width, text) of the instructions that use the offset off an object's register:
    the stack pointer, and the frame pointer where the function sets one up, are left out."""
    frame = any(i.mnemonic in ("lea", "mov") and i.op_str.startswith("rbp, ") and "rsp" in i.op_str for i in insns[:12])
    out = []
    for insn in insns:
        for op in insn.operands:
            if op.type != X86.X86_OP_MEM or op.mem.disp != disp:
                continue
            if op.mem.base in (0, X86.X86_REG_RIP, X86.X86_REG_RSP) or (frame and op.mem.base == X86.X86_REG_RBP):
                continue
            out.append((insn.mnemonic, op.size, "%s %s" % (insn.mnemonic, insn.op_str)))
    return out


def kinds(found):
    return {(m, s) for m, s, _ in found}


def cmd_members(b):
    _, members = reference_table()
    for name, off in members:
        cls, member = name.split(".", 1)
        if member == "Size":
            print("%s\t0x%x\tsize\tchecked at run time against the reflected struct" % (name, off))
            continue
        prefixes = [cls + "::"] + MEMBER_FUNCTIONS.get(cls, [])
        cands = []
        for r, s, n in b.rows:
            if 0x10 <= s <= 0x2000 and any(n.startswith(p) for p in prefixes):
                insns = disasm(b.ref, r, s)
                u = uses(insns, off)
                if u:
                    cands.append((not n.startswith(cls + "::"), s, r, n, insns, u))
        cands.sort()
        proofs = []
        for other, s, r, n, insns, u in cands[:20]:
            hit, _ = b.exact(r, s)
            if hit is not None:
                proofs.append("exact%s %s 0x%x->0x%x: %s" % (" (another class's)" if other else "", n, r, hit, u[0][2]))
                if len(proofs) >= 2:
                    break
        if not proofs:
            for other, s, r, n, insns, u in cands[:6]:
                res = b.find(r, s)
                if not res or res[0][0] < 0.6:
                    continue
                mine = uses(disasm(b.new, res[0][1], int(s * 1.3) + 0x80), off)
                if kinds(u) & kinds(mine):
                    proofs.append("near %.2f%s %s 0x%x->0x%x: %s" % (res[0][0], " (another class's)" if other else "", n, r, res[0][1], mine[0][2]))
                    break
        verdict = proofs[0].split(" ")[0] if proofs else ("UNPROVEN" if cands else "UNPROVEN: no function uses it")
        print("%s\t0x%x\t%s\t%s" % (name, off, verdict, "; ".join(proofs) or "%d functions, none carried over" % len(cands)))
        sys.stdout.flush()


IDENT = re.compile(rb"[A-Za-z_][A-Za-z0-9_]{1,80}\x00")


def property_records(image):
    _, base, _, data = image.section(".rdata")
    lo, hi = image.base + base, image.base + base + len(data)
    out = collections.Counter()
    for j in range(0, len(data) - 0x40, 8):
        (p,) = struct.unpack_from("<Q", data, j)
        if not lo <= p < hi:
            continue
        m = IDENT.match(data, p - lo)
        (repnotify,) = struct.unpack_from("<Q", data, j + 8)
        if not m or (repnotify and not lo <= repnotify < hi):
            continue
        dim, off = struct.unpack_from("<HH", data, j + 0x30)
        if dim == 1 and data[j + 24] <= 0x40:
            out[(m.group(0)[:-1].decode(), off)] += 1
    return out


def cmd_layout(b):
    a = property_records(b.ref)
    c = property_records(b.new)
    print("reflected property records: %d in the reference, %d in the other" % (sum(a.values()), sum(c.values())))
    gone, came = a - c, c - a
    moved = sorted({n for n, _ in gone} & {n for n, _ in came})
    print("moved:")
    for name in moved:
        print("  %s\t%s -> %s" % (name, sorted("0x%x" % o for n, o in gone if n == name), sorted("0x%x" % o for n, o in came if n == name)))
    for label, side in (("only in the reference", gone), ("only in the other", came)):
        names = sorted({"%s 0x%x" % (n, o) for n, o in side if n not in moved})
        print("%s (%d): %s" % (label, len(names), ", ".join(names)))


def cmd_side(b, ref_spec, other_rva):
    r, s = b.by_name[ref_spec] if ref_spec in b.by_name else (int(ref_spec, 16), 0x200)
    x, y = body(b.ref, r, s + 0x10), body(b.new, int(other_rva, 16), max(s * 2, 0x200))
    for i in range(max(len(x), len(y))):
        cells = []
        for insns in (x, y):
            if i < len(insns):
                insn = insns[i]
                t = (
                    " ->0x%x" % rip_target(insn)
                    if insn.disp_size and is_rip(insn)
                    else ""
                )
                cells.append(("%s %s%s" % (insn.mnemonic, insn.op_str, t))[:64])
            else:
                cells.append("")
        print("  %-64s | %s" % tuple(cells))


def cmd_callee(b, caller, callee):
    r, s = b.by_name[caller]
    target = b.by_name[callee][0]
    hit, insns = b.exact(r, s)
    if hit is None:
        raise SystemExit("%s has no exact match in the other build" % caller)
    mine = disasm(b.new, hit, s)
    for i, insn in enumerate(insns):
        if (
            is_branch(insn)
            and insn.operands
            and insn.operands[0].type == X86.X86_OP_IMM
            and insn.operands[0].imm == target
            and i < len(mine)
        ):
            print(
                "%s 0x%x -> 0x%x, by %s's %s at +0x%x"
                % (
                    callee,
                    target,
                    mine[i].operands[0].imm,
                    caller,
                    insn.mnemonic,
                    insn.address - r,
                )
            )


def main():
    args = sys.argv[1:]
    if not args or args[0] in ("-h", "--help"):
        print(__doc__)
        return 0
    command = args[0]
    if command == "side":
        cmd_side(Builds(os.environ.get("DA_OTHER_EXE", "")), args[1], args[2])
    elif command == "callee":
        cmd_callee(Builds(args[3]), args[1], args[2])
    elif command in ("places", "members", "layout"):
        {"places": cmd_places, "members": cmd_members, "layout": cmd_layout}[command](
            Builds(args[1])
        )
    else:
        print(__doc__)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
