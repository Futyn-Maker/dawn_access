"""Makes the game's save (SaveGame0.sav) start its act again from the act's first node, through the game's
own path for a save its Lua scripts can no longer read.

On load (UBatesSaveGame::Serialize_Internal, mode 2) a save whose LuaStateVersion is not the build's (8 in
build 20076348, UBatesSaveSettings) is made minimal: its level becomes the act's node 1 and its section that
node's (BatesNodeLevelSectionData), bSaveLoadEpisodeSnapshot is set, and SerializeGlobalStateSnapshot gives the
global state the act's own snapshot (GlobalStateSnapshots[act*10000 + part*100 + 1], else [... + 0], written
as the act began). Continue then loads the section without the world, the player or the Lua state, and the
act's script runs from its start (UBatesLuaScriptUpdater::OnFinishedStreamingRootLevel). The first full
checkpoint saves normally again (SaveGame_Internal clears bIsMinimalSave). For this, only the version changes.

What no snapshot holds is put back where the save itself shows how the act found it:
- a totem or clue the tracker holds as found (TotemTracker, ClueTracker: not EBatesSeenStatus::Locked) whose
  callforward bool is false in the snapshot was found in this act: its status goes back to Locked, which the
  game records for one not found (UBatesSeenStatusTracker::GetSeenStatusForTotem); the totem itself follows
  the bool (ABatesInteractionTotem::InitialiseInteraction), so its vision plays again;
- with --relations-seen CHARACTER (repeatable): that character's traits and relationships go back to the
  values the status page last showed. Each record holds the value and the value last shown; the game's
  adjustments change only the first (UBatesTraitsRelationshipsManager::AdjustCharacterTrait and
  AdjustCharacterRelationship). Right only for a character whose page was not looked at during the act and
  whose values nothing changed earlier since it was last looked at: the report lists every record that
  differs, to check against the act's choices and the session log.

python -I tools/saves/restart_act.py <SaveGame0.sav>                         report only
python -I tools/saves/restart_act.py <SaveGame0.sav> --write <out.sav> [--relations-seen Mike]

The file is signed: FCrc::MemCrc32(body, 0xFFFFFFFF), big-endian, after a kind byte 0xEA, where the body is
the GVAS data padded with zeros to a multiple of 4 (UGameplayStatics::SaveGameToSlotSigned). The output is
re-signed, read back whole and compared with the input byte by byte before it is kept. Put it in place with
the game closed: scripts\\save-restore.ps1 -Checkpoint <out.sav> (it backs the current save up first).
"""

import argparse
import struct
import sys
import zlib

# EBatesCharacter; the first ten have rows of their own.
CHARACTER_NAMES = [
    "Beth",
    "Hannah",
    "Sam",
    "Mike",
    "Jessica",
    "Emily",
    "Matt",
    "Chris",
    "Ashley",
    "Josh",
    "Jack",
    "Psycho",
    "Psychiatrist",
    "Wendigo",
]
CHARACTERS = CHARACTER_NAMES[:10]
TRAITS = ["Honest", "Charitable", "Funny", "Brave", "Romantic", "Curious"]
STATE_MAPS = ("StateBools", "StateInts", "StateFloats", "StateStrings")
LOCKED = "EBatesSeenStatus::Locked"
SIGN_KIND = 0xEA


class Reader:
    def __init__(self, data, pos=0):
        self.data = data
        self.pos = pos

    def i32(self):
        v = struct.unpack_from("<i", self.data, self.pos)[0]
        self.pos += 4
        return v

    def u8(self):
        v = self.data[self.pos]
        self.pos += 1
        return v

    def fstring(self):
        """(text, start, end): an FString and the bytes it takes, its length field included."""
        start = self.pos
        n = self.i32()
        if n == 0:
            return "", start, self.pos
        if n < 0:
            raw = self.data[self.pos : self.pos - 2 * n]
            self.pos += -2 * n
            return raw.decode("utf-16-le").rstrip("\x00"), start, self.pos
        raw = self.data[self.pos : self.pos + n]
        self.pos += n
        return raw.decode("latin-1").rstrip("\x00"), start, self.pos


class Prop:
    def __init__(self, name, ptype, size, size_at, extra, start):
        self.name, self.ptype, self.size, self.size_at, self.extra, self.start = (
            name,
            ptype,
            size,
            size_at,
            extra,
            start,
        )

    @property
    def end(self):
        return self.start + self.size


def read_header(r):
    """One tagged property's header, or None at the list's "None"."""
    name = r.fstring()[0]
    if name == "None":
        return None
    ptype = r.fstring()[0]
    size_at = r.pos
    size = r.i32()
    r.i32()  # array index
    extra = None
    if ptype == "StructProperty":
        extra = r.fstring()[0]
        r.pos += 16
    elif ptype == "BoolProperty":
        extra = r.u8()
    elif ptype in ("EnumProperty", "ByteProperty", "ArrayProperty", "SetProperty"):
        extra = r.fstring()[0]
    elif ptype == "MapProperty":
        extra = (r.fstring()[0], r.fstring()[0])
    if r.u8():
        r.pos += 16
    return Prop(name, ptype, size, size_at, extra, r.pos)


def walk(data, start, end):
    """The properties of a tagged list from start up to its "None": {name: Prop} and where the list ends."""
    r = Reader(data, start)
    props = {}
    while r.pos < end:
        p = read_header(r)
        if p is None:
            return props, r.pos
        props[p.name] = p
        r.pos = p.end
    raise ValueError("no None before %d" % end)


def top_level(data):
    """The save's top-level properties and where its GVAS data ends."""
    if data[:4] != b"GVAS":
        raise ValueError("not a GVAS save")
    r = Reader(data, 4)
    r.i32(), r.i32(), r.i32()  # save game version, package versions
    r.pos += 10  # engine major, minor, patch, changelist
    r.fstring()  # branch
    r.i32()  # custom version format
    custom_versions = r.i32()
    r.pos += custom_versions * 20
    r.fstring()  # save game class
    props, after_none = walk(data, r.pos, len(data))
    return (
        props,
        after_none + 4,
    )  # UObject::Serialize writes a zero "has GUID" after the list


def signed_end(content_end):
    return content_end + (-content_end) % 4


def check_signature(data, content_end):
    body_end = signed_end(content_end)
    if len(data) != body_end + 5 or data[body_end] != SIGN_KIND:
        return False
    return (
        zlib.crc32(data[:body_end], 0xFFFFFFFF).to_bytes(4, "big")
        == data[body_end + 1 :]
    )


def sign(body):
    body = body + b"\x00" * ((-len(body)) % 4)
    return body + bytes([SIGN_KIND]) + zlib.crc32(body, 0xFFFFFFFF).to_bytes(4, "big")


def state_value(data, p):
    if p.ptype == "BoolProperty":
        return bool(p.extra)
    if p.ptype == "IntProperty":
        return struct.unpack_from("<i", data, p.start)[0]
    if p.ptype == "FloatProperty":
        return round(struct.unpack_from("<f", data, p.start)[0], 6)
    if p.ptype in ("StrProperty", "NameProperty", "EnumProperty"):
        return Reader(data, p.start).fstring()[0]
    return None


def state_maps(data, struct_start, struct_end):
    """The four state maps of a FBatesGlobalStateSaveData: {map: {name: value}}."""
    props, _ = walk(data, struct_start, struct_end)
    out = {}
    for m in STATE_MAPS:
        p = props.get(m)
        if p is None:
            continue
        r = Reader(data, p.start)
        r.i32()  # keys to remove
        values = {}
        for _ in range(r.i32()):
            key = r.fstring()[0]
            fields, r.pos = walk(data, r.pos, p.end)
            v = fields.get("bValue") or fields.get("Value")
            values[key] = state_value(data, v) if v else None
        out[m] = values
    return out


def snapshots(data, p):
    """GlobalStateSnapshots: {key: (struct start, struct end)}."""
    r = Reader(data, p.start)
    r.i32()
    out = {}
    for _ in range(r.i32()):
        key = r.i32()
        start = r.pos
        _, r.pos = walk(data, start, p.end)
        out[key] = (start, r.pos)
    return out


def tracker(data, p):
    """A FString -> enum-name map: [(key, value, value start, value end)]."""
    r = Reader(data, p.start)
    r.i32()
    out = []
    for _ in range(r.i32()):
        key = r.fstring()[0]
        value, vs, ve = r.fstring()
        out.append((key, value, vs, ve))
    return out


def relations(data, p):
    """CharacterRelationshipData's records: [(row, kind, other, value, shown, offset of value)]."""
    count = struct.unpack_from("<i", data, p.start)[0]
    at = p.start + 4
    if count != len(CHARACTERS) * 16 * 9:
        raise ValueError("unexpected CharacterRelationshipData size %d" % count)
    out = []
    for row in CHARACTERS:
        for kind, n, names in (
            ("relationship", 10, CHARACTER_NAMES),
            ("trait", 6, TRAITS),
        ):
            for _ in range(n):
                index, value, shown = struct.unpack_from("<Bff", data, at)
                out.append((row, kind, names[index], value, shown, at + 1))
                at += 9
    return out


def fstring_bytes(text):
    raw = text.encode("latin-1") + b"\x00"
    return struct.pack("<i", len(raw)) + raw


def plan(data, relations_seen):
    """The report's lines and the edits: [(offset, old bytes, new bytes, sizes to grow)]."""
    top, content_end = top_level(data)
    lines, edits = [], []
    if not check_signature(data, content_end):
        raise ValueError(
            "the save's signature does not match: not a save this tool understands"
        )
    level = walk(data, top["LevelData"].start, top["LevelData"].end)[0]
    act = struct.unpack_from("<i", data, level["Act"].start)[0]
    part = struct.unpack_from("<i", data, level["Part"].start)[0]
    version_at = top["LuaStateVersion"].start
    version = struct.unpack_from("<i", data, version_at)[0]
    lines.append(
        "act %d part %d, LuaStateVersion %d, LevelStartingNode %d, section %s"
        % (
            act,
            part,
            version,
            struct.unpack_from("<i", data, top["LevelStartingNode"].start)[0],
            Reader(data, top["LevelStreamingSectionName"].start).fstring()[0],
        )
    )
    if act == 1 and part == 1:
        raise ValueError(
            "the prologue's start resets the global state instead: start a new story"
        )
    game = walk(data, top["GameSaveData"].start, top["GameSaveData"].end)[0]
    snaps = snapshots(data, game["GlobalStateSnapshots"])
    base = act * 10000 + part * 100
    key = base + 1 if base + 1 in snaps else base
    if key not in snaps:
        raise ValueError(
            "no snapshot of act %d part %d in the save (keys %s)"
            % (act, part, sorted(snaps))
        )
    lines.append(
        "the act's snapshot %d gives the global state; restarting undoes:" % key
    )
    now = state_maps(
        data, top["GlobalStateSaveData"].start, top["GlobalStateSaveData"].end
    )
    then = state_maps(data, *snaps[key])
    for m in STATE_MAPS:
        a, b = then.get(m, {}), now.get(m, {})
        for name in sorted(set(a) | set(b)):
            if a.get(name) != b.get(name):
                lines.append(
                    "  %s %s: %r now, %r as the act began"
                    % (m, name, b.get(name), a.get(name))
                )
    edits.append(
        (version_at, struct.pack("<i", version), struct.pack("<i", version - 1), [])
    )
    lines.append(
        "LuaStateVersion %d -> %d: the game restarts the act at node 1 with that snapshot"
        % (version, version - 1)
    )
    bools = then.get("StateBools", {})
    for tracker_name in ("TotemTracker", "ClueTracker"):
        p = game[tracker_name]
        for name, status, vs, ve in tracker(data, p):
            if status != LOCKED and bools.get(name) is False:
                lines.append(
                    "%s %s: %s -> %s (found in this act)"
                    % (tracker_name, name, status, LOCKED)
                )
                edits.append(
                    (
                        vs,
                        data[vs:ve],
                        fstring_bytes(LOCKED),
                        [top["GameSaveData"].size_at, p.size_at],
                    )
                )
    rel = relations(data, game["CharacterRelationshipData"])
    lines.append(
        "traits and relationships differing from what the status page last showed:"
    )
    for row, kind, other, value, shown, at in rel:
        if value != shown:
            fix = row in relations_seen
            lines.append(
                "  %s %s %s: %g, shown %g%s"
                % (row, kind, other, value, shown, " -> %g" % shown if fix else "")
            )
            if fix:
                edits.append(
                    (at, struct.pack("<f", value), struct.pack("<f", shown), [])
                )
    unknown = [c for c in relations_seen if c not in CHARACTERS]
    if unknown:
        raise ValueError("no such character: %s" % ", ".join(unknown))
    return lines, edits, content_end


def apply(data, edits, content_end):
    """The edited GVAS data (unsigned): edits in place or resized, the sizes around them grown."""
    out = bytearray(data[:content_end])
    for offset, old, new, sizes in sorted(edits, key=lambda e: -e[0]):
        if bytes(out[offset : offset + len(old)]) != old:
            raise ValueError("unexpected bytes at %d" % offset)
        out[offset : offset + len(old)] = new
        grow = len(new) - len(old)
        for size_at in sizes:
            if size_at > offset:
                raise ValueError("a size field after its edit at %d" % offset)
            size = struct.unpack_from("<i", out, size_at)[0]
            struct.pack_into("<i", out, size_at, size + grow)
    return bytes(out)


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("save")
    ap.add_argument("--write", metavar="OUT")
    ap.add_argument(
        "--relations-seen", action="append", default=[], metavar="CHARACTER"
    )
    args = ap.parse_args()
    data = open(args.save, "rb").read()
    try:
        lines, edits, content_end = plan(data, args.relations_seen)
    except ValueError as e:
        print("cannot restart: %s" % e)
        return 1
    print("\n".join(lines))
    if not args.write:
        return 0
    edited = sign(apply(data, edits, content_end))
    # Read back: the whole save must parse, be signed, and hold what was planned and nothing else.
    _, end = top_level(edited)
    if not check_signature(edited, end):
        print("cannot restart: the written save does not verify")
        return 1
    if len(edited) == len(data):
        changed = [i for i in range(len(data) - 4) if data[i] != edited[i]]
        expected = set()
        for offset, old, new, _ in edits:
            expected.update(
                offset + i for i in range(len(old)) if old[i : i + 1] != new[i : i + 1]
            )
        if set(changed) != expected:
            print(
                "cannot restart: bytes beyond the plan changed: %s"
                % sorted(set(changed) ^ expected)[:20]
            )
            return 1
    again, _, _ = plan(edited, [])
    print("written %s (%d edits); read back:" % (args.write, len(edits)))
    print("\n".join("  " + line for line in again[:3]))
    with open(args.write, "wb") as f:
        f.write(edited)
    return 0


if __name__ == "__main__":
    sys.exit(main())
