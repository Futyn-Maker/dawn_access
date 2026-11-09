"""Symbol tables of the game's program database (Bates-Win64-Shipping.pdb, shipped beside the
executable) through the DIA SDK (msdia140.dll of Visual Studio).

usage: python -I tools/exe/pdb_symbols.py <command>
  dump              write functions.tsv, publics.tsv and canary.json into the symbol folder
  query <pattern>   print the functions and publics whose name holds the pattern (case-sensitive;
                    * and ? are wildcards, and a pattern with one must match the whole name)
  udt <pattern>     print the layout (members, offsets, sizes) of the UDTs whose whole name
                    matches the pattern (* and ? are wildcards)
  vtable <class>    print the virtual functions of a class in vtable order (from the UDT)
  bytes <name> [n]  print the first n bytes (default 32) of a function from the executable

The symbol folder is research/symbols of this repository, or DA_SYMBOLS; the game's folder is
DA_GAME_DIR or Steam's default; msdia140.dll is DA_MSDIA or found in Visual Studio 2022.
functions.tsv (rva, length, name) is what dasm.py, callers.py and crash.py name code by.

The canary is the PDB's GUID and age, and the executable's debug directory must name the same PDB:
anything built on these tables checks that before trusting an RVA (the mod: core/Symbols).
"""

import ctypes
import fnmatch
import json
import os
import struct
import sys
import time

import comtypes
import comtypes.client
from comtypes import GUID
from comtypes.server import IClassFactory

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
HERE = os.environ.get("DA_SYMBOLS", os.path.join(REPO, "research", "symbols"))
GAME = os.environ.get(
    "DA_GAME_DIR", r"C:\Program Files (x86)\Steam\steamapps\common\Until Dawn"
)
EXE = os.path.join(GAME, r"Windows\Bates\Binaries\Win64\Bates-Win64-Shipping.exe")
PDB = os.path.join(GAME, r"Windows\Bates\Binaries\Win64\Bates-Win64-Shipping.pdb")
CLSID_DIA_SOURCE = GUID("{E6756135-1E65-4D17-8576-610761398C3C}")

(
    SymTagNull,
    SymTagExe,
    SymTagCompiland,
    SymTagCompilandDetails,
    SymTagCompilandEnv,
    SymTagFunction,
    SymTagBlock,
    SymTagData,
) = range(8)
(
    SymTagAnnotation,
    SymTagLabel,
    SymTagPublicSymbol,
    SymTagUDT,
    SymTagEnum,
    SymTagFunctionType,
    SymTagPointerType,
    SymTagArrayType,
) = range(8, 16)
SymTagBaseType, SymTagTypedef, SymTagBaseClass, SymTagFriend, SymTagFunctionArgType = (
    range(16, 21)
)
NS_REGEX = 0x8
NS_CASE_SENSITIVE = 0x1


def find_dia():
    """msdia140.dll: DA_MSDIA, else the DIA SDK of an edition of Visual Studio 2022."""
    if os.environ.get("DA_MSDIA"):
        return os.environ["DA_MSDIA"]
    for root in (
        r"C:\Program Files (x86)\Microsoft Visual Studio\2022",
        r"C:\Program Files\Microsoft Visual Studio\2022",
    ):
        for edition in ("BuildTools", "Community", "Professional", "Enterprise"):
            path = os.path.join(root, edition, r"DIA SDK\bin\amd64\msdia140.dll")
            if os.path.exists(path):
                return path
    raise SystemExit(
        "msdia140.dll not found: set DA_MSDIA to it (Visual Studio's DIA SDK)"
    )


def open_session():
    dia = find_dia()
    msdia = comtypes.client.GetModule(dia)
    dll = ctypes.OleDLL(dia)
    factory = ctypes.POINTER(IClassFactory)()
    dll.DllGetClassObject(
        ctypes.byref(CLSID_DIA_SOURCE),
        ctypes.byref(IClassFactory._iid_),
        ctypes.byref(factory),
    )
    source = factory.CreateInstance(None, msdia.IDiaDataSource)
    source.loadDataFromPdb(PDB)
    session = source.openSession()
    return msdia, session, session.globalScope


def exe_debug_directory():
    """The PDB GUID, age and path the exe's debug directory names, with the exe's size and timestamp."""
    import pefile

    pe = pefile.PE(EXE, fast_load=True)
    pe.parse_data_directories(
        directories=[pefile.DIRECTORY_ENTRY["IMAGE_DIRECTORY_ENTRY_DEBUG"]]
    )
    info = {
        "exe_size": os.path.getsize(EXE),
        "exe_timestamp": pe.FILE_HEADER.TimeDateStamp,
    }
    for entry in getattr(pe, "DIRECTORY_ENTRY_DEBUG", []):
        if entry.struct.Type != 2:  # IMAGE_DEBUG_TYPE_CODEVIEW
            continue
        data = pe.get_data(entry.struct.AddressOfRawData, entry.struct.SizeOfData)
        if data[:4] != b"RSDS":
            continue
        guid = GUID()
        ctypes.memmove(ctypes.byref(guid), data[4:20], 16)
        info["pdb_guid"] = str(guid)
        info["pdb_age"] = struct.unpack_from("<I", data, 20)[0]
        info["pdb_path"] = data[24:].split(b"\0", 1)[0].decode("latin-1")
    return info


def canary(scope):
    info = exe_debug_directory()
    info["scope_guid"] = str(scope.guid)
    info["scope_age"] = scope.age
    info["matches"] = (
        info.get("pdb_guid", "").lower() == str(scope.guid).lower()
        and info.get("pdb_age") == scope.age
    )
    return info


def dump():
    t = time.time()
    os.makedirs(HERE, exist_ok=True)
    msdia, session, scope = open_session()
    info = canary(scope)
    print("canary:", json.dumps(info))
    with open(os.path.join(HERE, "canary.json"), "w", encoding="utf-8") as f:
        json.dump(info, f, indent=2)
    for tag, name in ((SymTagFunction, "functions"), (SymTagPublicSymbol, "publics")):
        rows = scope.findChildren(tag, None, 0)
        count = rows.Count
        path = os.path.join(HERE, name + ".tsv")
        written = 0
        with open(path, "w", encoding="utf-8", newline="\n") as f:
            f.write("rva\tlength\tname\n")
            for i in range(count):
                s = rows.Item(i)
                rva = s.relativeVirtualAddress
                if rva == 0:
                    continue
                length = s.length if tag == SymTagFunction else 0
                f.write("%x\t%x\t%s\n" % (rva, length, s.name))
                written += 1
                if i % 100000 == 0:
                    print(
                        "  %s %d/%d (%.0fs)" % (name, i, count, time.time() - t),
                        flush=True,
                    )
        print("%s: %d rows -> %s" % (name, written, path))
    print("done in %.0fs" % (time.time() - t))


def as_wildcards(pattern):
    """The DIA SDK matches names by wildcards (* and ?), never a regular expression: a pattern
    without one is looked for anywhere in the name."""
    return pattern if any(c in pattern for c in "*?") else "*" + pattern + "*"


def query(pattern):
    msdia, session, scope = open_session()
    pattern = as_wildcards(pattern)
    for tag, label in ((SymTagFunction, "func"), (SymTagPublicSymbol, "pub")):
        rows = scope.findChildren(tag, pattern, NS_REGEX | NS_CASE_SENSITIVE)
        for i in range(rows.Count):
            s = rows.Item(i)
            if not fnmatch.fnmatchcase(s.name, pattern):
                continue
            extra = " len=%x" % s.length if tag == SymTagFunction else ""
            print("%s rva=%08x%s %s" % (label, s.relativeVirtualAddress, extra, s.name))


def type_name(t):
    try:
        if t is None:
            return "?"
        tag = t.symTag
        if tag == SymTagPointerType:
            return type_name(t.type) + ("&" if t.reference else "*")
        if tag == SymTagArrayType:
            return "%s[%d]" % (type_name(t.type), t.count)
        if tag == SymTagBaseType:
            return {
                1: "void",
                2: "char",
                3: "wchar_t",
                6: "int",
                7: "uint",
                8: "float",
                10: "bool",
                13: "long",
                14: "ulong",
                25: "char16",
                26: "char32",
                29: "bool",
            }.get(t.baseType, "base%d" % t.baseType) + str(t.length)
        name = t.name
        return name if name else "<%d>" % tag
    except Exception:
        return "?"


def udt(pattern):
    msdia, session, scope = open_session()
    rows = scope.findChildren(SymTagUDT, pattern, NS_REGEX | NS_CASE_SENSITIVE)
    seen = set()
    for i in range(rows.Count):
        u = rows.Item(i)
        if u.name in seen or u.length == 0:
            continue
        seen.add(u.name)
        print("UDT %s size=0x%x" % (u.name, u.length))
        bases = u.findChildren(SymTagBaseClass, None, 0)
        for b in range(bases.Count):
            base = bases.Item(b)
            print("  base %s at 0x%x" % (base.name, base.offset))
        members = u.findChildren(SymTagData, None, 0)
        for m in range(members.Count):
            d = members.Item(m)
            if d.locationType != 4:  # LocIsThisRel
                continue
            print("  +0x%04x %s %s" % (d.offset, type_name(d.type), d.name))


def vtable(class_name):
    msdia, session, scope = open_session()
    rows = scope.findChildren(SymTagUDT, class_name, NS_CASE_SENSITIVE)
    for i in range(rows.Count):
        u = rows.Item(i)
        if u.length == 0:
            continue
        funcs = u.findChildren(SymTagFunction, None, 0)
        slots = []
        for f in range(funcs.Count):
            fn = funcs.Item(f)
            if fn.virtual:
                slots.append((fn.virtualBaseOffset, fn.name))
        for off, name in sorted(slots):
            print("  slot %3d (+0x%03x) %s" % (off // 8, off, name))
        break


def func_bytes(name, count=32):
    msdia, session, scope = open_session()
    rows = scope.findChildren(SymTagFunction, name, NS_CASE_SENSITIVE)
    if rows.Count == 0:
        rows = scope.findChildren(SymTagPublicSymbol, name, NS_CASE_SENSITIVE)
    if rows.Count == 0:
        print("no such symbol")
        return
    import pefile

    pe = pefile.PE(EXE, fast_load=True)
    for i in range(rows.Count):
        s = rows.Item(i)
        rva = s.relativeVirtualAddress
        data = pe.get_data(rva, count)
        print("%s rva=%08x: %s" % (s.name, rva, " ".join("%02X" % b for b in data)))


if __name__ == "__main__":
    cmd = sys.argv[1] if len(sys.argv) > 1 else ""
    if cmd == "dump":
        dump()
    elif cmd == "query" and len(sys.argv) > 2:
        query(sys.argv[2])
    elif cmd == "udt" and len(sys.argv) > 2:
        udt(sys.argv[2])
    elif cmd == "vtable" and len(sys.argv) > 2:
        vtable(sys.argv[2])
    elif cmd == "bytes" and len(sys.argv) > 2:
        func_bytes(sys.argv[2], int(sys.argv[3]) if len(sys.argv) > 3 else 32)
    else:
        print(__doc__)
