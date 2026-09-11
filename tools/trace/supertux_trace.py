#!/usr/bin/env python3
# SuperTux
# Copyright (C) 2026 Abdelrahman Madkour <abdelrahman.madkour@patronus.ai>
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <http://www.gnu.org/licenses/>.

"""Reference reader/writer for SuperTux trace files (format version 2).

Standard library only, by design: a tool that does not link the game must be
able to read and write traces. This module is the format's executable
specification for outside consumers, so it is deliberately short. If it grows
much past this size, the binary format has become too complicated and the
format is what should change.

File layout:

    "STGT" | uint16 format_version | uint32 inflated_size | deflate(chunk*)
    chunk := name (NUL-terminated ASCII) | uint32 length | length bytes

All integers little-endian; all floats IEEE-754 binary32. Unknown chunks are
preserved on a round-trip.
"""

from __future__ import annotations

import argparse
import json
import math
import struct
import sys
import zlib
from dataclasses import dataclass, field
from enum import IntEnum
from pathlib import Path
from typing import List, Tuple

MAGIC = b"STGT"
FORMAT_VERSION = 2
RESERVED_MASK = 0xF800
VISUAL_FRAME_BYTES = 21
MAX_RUN_LENGTH = 65535
MAX_STRING_TABLE = 65535
# zlib's worst-case expansion ratio for a valid deflate stream, and an
# absolute ceiling on a declared inflated_size. Both mirror the constants of
# the same name in src/trace/trace_file.cpp -- without them a 204 KB file can
# cost 410 MB of RSS here while the C++ side rejects it outright, which would
# make the two implementations disagree about what is valid. See the README's
# validation rules for the 256 MiB rationale.
MAX_DEFLATE_EXPANSION_RATIO = 1032
MAX_INFLATED_BYTES = 256 * 1024 * 1024
KNOWN_CHUNKS = frozenset(
    {
        "header",
        "inputs",
        "start",
        "actions",
        "sectors",
        "transitions",
        "visual",
        "notes",
    }
)


class TraceError(Exception):
    """Raised on any malformed input or invalid trace."""


def f32(value: float) -> float:
    """Rounds a Python float to the float32 precision the format carries.

    Without this, a Header built in Python holds doubles while a Header read
    back from a file holds float32-rounded values, so dataclass equality --
    which the tests rely on -- would fail for values like 1000.0 / 15.0. Header
    and VisualFrame normalize their float fields through this on construction,
    which makes round-trips exact rather than approximate.
    """
    try:
        return struct.unpack("<f", struct.pack("<f", value))[0]
    except (struct.error, OverflowError) as exc:
        raise TraceError(f"value does not fit in a float32: {value!r}") from exc


class Action(IntEnum):
    LEFT = 0
    RIGHT = 1
    UP = 2
    DOWN = 3
    JUMP = 4
    ACTION = 5
    ITEM = 6
    PEEK_LEFT = 7
    PEEK_RIGHT = 8
    PEEK_UP = 9
    PEEK_DOWN = 10


class ProducerKind(IntEnum):
    UNKNOWN = 0
    HUMAN = 1
    SCRIPT = 2
    AGENT = 3


class Outcome(IntEnum):
    INCOMPLETE = 0
    COMPLETED = 1
    DIED = 2
    TIMEOUT = 3
    ABORTED = 4


class ScreenShakeMode(IntEnum):
    OFF = 0
    REDUCED = 1
    FULL = 2


class StartKind(IntEnum):
    """How the recorded episode's step 0 came to be.

    UNKNOWN is not a missing value: it is the recorder saying it could not
    represent the session's start state, and it is the one value that forbids
    a `start` chunk.
    """

    LEVEL_START = 0
    RESPAWN = 1
    CHECKPOINT = 2
    UNKNOWN = 3


class Bonus(IntEnum):
    """Mirrors BonusType in src/supertux/player_status.hpp."""

    NONE = 0
    GROWUP = 1
    FIRE = 2
    ICE = 3
    AIR = 4
    EARTH = 5


@dataclass
class StartState:
    """The state a GameSession carries across restart_level().

    Mirrors trace::StartState in src/trace/trace_start.hpp; serialization
    order is the field order below.
    """

    sector: str = ""
    spawnpoint: str = ""
    position_x: float = 0.0
    position_y: float = 0.0
    is_checkpoint: bool = False
    play_time: float = 0.0
    coins: int = 0
    tuxdolls: int = 0
    bonus: Bonus = Bonus.NONE
    item_pocket: Bonus = Bonus.NONE
    coins_at_start: int = 0
    bonus_at_start: Bonus = Bonus.NONE
    pocket_at_start: Bonus = Bonus.NONE

    def __post_init__(self) -> None:
        self.position_x = f32(self.position_x)
        self.position_y = f32(self.position_y)
        self.play_time = f32(self.play_time)


@dataclass
class Header:
    engine_version: str = ""
    platform: str = ""
    level_path: str = ""
    level_md5: bytes = b"\x00" * 16
    seed: int = 0
    game_time_origin: float = 0.0
    step_rate: float = 0.0
    step_count: int = 0
    play_time: float = 0.0
    producer_kind: ProducerKind = ProducerKind.UNKNOWN
    provenance: str = ""
    outcome: Outcome = Outcome.INCOMPLETE
    deaths: int = 0
    coins: int = 0
    viewport_width: int = 0
    viewport_height: int = 0
    screen_shake_mode: ScreenShakeMode = ScreenShakeMode.FULL
    camera_peek_multiplier: float = 1.0
    # UNKNOWN by default because a default-constructed header has captured
    # nothing; mirrors trace::Header in src/trace/trace_header.hpp.
    start_kind: StartKind = StartKind.UNKNOWN

    def __post_init__(self) -> None:
        self.game_time_origin = f32(self.game_time_origin)
        self.step_rate = f32(self.step_rate)
        self.play_time = f32(self.play_time)
        self.camera_peek_multiplier = f32(self.camera_peek_multiplier)


@dataclass
class VisualFrame:
    pos_x: float = 0.0
    pos_y: float = 0.0
    vel_x: float = 0.0
    vel_y: float = 0.0
    action_id: int = 0
    frame_idx: int = 0
    frame_progress: int = 0
    flags: int = 0

    def __post_init__(self) -> None:
        self.pos_x = f32(self.pos_x)
        self.pos_y = f32(self.pos_y)
        self.vel_x = f32(self.vel_x)
        self.vel_y = f32(self.vel_y)


@dataclass
class Trace:
    header: Header = field(default_factory=Header)
    inputs: List[int] = field(default_factory=list)
    visual: List[VisualFrame] = field(default_factory=list)
    actions: List[str] = field(default_factory=list)
    sectors: List[str] = field(default_factory=list)
    sector_transitions: List[Tuple[int, int]] = field(default_factory=list)
    # Present exactly when header.start_kind is not UNKNOWN.
    start: StartState | None = None
    episode_note: bytes = b""
    step_notes: List[Tuple[int, bytes]] = field(default_factory=list)
    unknown_chunks: List[Tuple[str, bytes]] = field(default_factory=list)


class _Reader:
    def __init__(self, data: bytes) -> None:
        self._data = data
        self._pos = 0

    def _need(self, n: int) -> None:
        if self._pos + n > len(self._data):
            raise TraceError("read past end of buffer")

    def u8(self) -> int:
        self._need(1)
        value = self._data[self._pos]
        self._pos += 1
        return value

    def _unpack(self, fmt: str, size: int):
        self._need(size)
        value = struct.unpack_from(fmt, self._data, self._pos)[0]
        self._pos += size
        return value

    def u16(self) -> int:
        return self._unpack("<H", 2)

    def u32(self) -> int:
        return self._unpack("<I", 4)

    def i32(self) -> int:
        return self._unpack("<i", 4)

    def f32(self) -> float:
        return self._unpack("<f", 4)

    def string(self) -> str:
        end = self._data.find(b"\x00", self._pos)
        if end < 0:
            raise TraceError("unterminated string")
        # The header's free-form strings are UTF-8. A decode failure must
        # surface as TraceError: a bare UnicodeDecodeError is not caught by
        # `except TraceError`, so a caller guarding a parse would crash on a
        # malformed file. Note the asymmetry with C++, which does not validate
        # UTF-8 -- see the README's validation rules.
        try:
            value = self._data[self._pos : end].decode("utf-8")
        except UnicodeDecodeError as exc:
            raise TraceError(f"string is not valid UTF-8: {exc}") from exc
        self._pos = end + 1
        return value

    def raw(self, n: int) -> bytes:
        self._need(n)
        value = self._data[self._pos : self._pos + n]
        self._pos += n
        return value

    @property
    def remaining(self) -> int:
        return len(self._data) - self._pos

    @property
    def exhausted(self) -> bool:
        return self._pos >= len(self._data)


class _Writer:
    def __init__(self) -> None:
        self._parts: List[bytes] = []

    def _pack(self, fmt: str, v) -> None:
        """Every out-of-range value must fail as TraceError.

        struct raises struct.error for an integer outside the field's range
        and OverflowError for a float too large for a float32; neither is
        caught by `except TraceError`, so a caller writing a trace with, say,
        sector_id > 65535 would see the wrong exception type escape.
        """
        try:
            self._parts.append(struct.pack(fmt, v))
        except (struct.error, OverflowError) as exc:
            raise TraceError(f"value {v!r} does not fit format '{fmt}'") from exc

    def u8(self, v: int) -> None:
        self._pack("<B", v)

    def u16(self, v: int) -> None:
        self._pack("<H", v)

    def u32(self, v: int) -> None:
        self._pack("<I", v)

    def i32(self, v: int) -> None:
        self._pack("<i", v)

    def f32(self, v: float) -> None:
        self._pack("<f", v)

    def string(self, v: str) -> None:
        try:
            encoded = v.encode("utf-8")
        except UnicodeEncodeError as exc:
            raise TraceError(f"string is not encodable as UTF-8: {exc}") from exc
        if b"\x00" in encoded:
            raise TraceError("string contains an embedded NUL")
        self._parts.append(encoded + b"\x00")

    def raw(self, v: bytes) -> None:
        self._parts.append(v)

    def data(self) -> bytes:
        return b"".join(self._parts)


def _encode_header(header: Header) -> bytes:
    w = _Writer()
    w.string(header.engine_version)
    w.string(header.platform)
    w.string(header.level_path)
    if len(header.level_md5) != 16:
        raise TraceError("level_md5 must be exactly 16 bytes")
    w.raw(header.level_md5)
    w.i32(header.seed)
    w.f32(header.game_time_origin)
    w.f32(header.step_rate)
    w.u32(header.step_count)
    w.f32(header.play_time)
    w.u8(int(header.producer_kind))
    w.string(header.provenance)
    w.u8(int(header.outcome))
    w.u32(header.deaths)
    w.u32(header.coins)
    w.u32(header.viewport_width)
    w.u32(header.viewport_height)
    w.u8(int(header.screen_shake_mode))
    w.f32(header.camera_peek_multiplier)
    w.u8(int(header.start_kind))
    return w.data()


def _decode_header(payload: bytes) -> Header:
    r = _Reader(payload)
    header = Header()
    header.engine_version = r.string()
    header.platform = r.string()
    header.level_path = r.string()
    header.level_md5 = r.raw(16)
    header.seed = r.i32()
    header.game_time_origin = r.f32()
    header.step_rate = r.f32()
    header.step_count = r.u32()
    header.play_time = r.f32()

    producer = r.u8()
    if producer > int(ProducerKind.AGENT):
        raise TraceError("unknown producer kind")
    header.producer_kind = ProducerKind(producer)

    header.provenance = r.string()

    outcome = r.u8()
    if outcome > int(Outcome.ABORTED):
        raise TraceError("unknown outcome")
    header.outcome = Outcome(outcome)

    header.deaths = r.u32()
    header.coins = r.u32()

    header.viewport_width = r.u32()
    header.viewport_height = r.u32()

    shake = r.u8()
    if shake > int(ScreenShakeMode.FULL):
        raise TraceError("unknown screen shake mode")
    header.screen_shake_mode = ScreenShakeMode(shake)

    header.camera_peek_multiplier = r.f32()

    start = r.u8()
    if start > int(StartKind.UNKNOWN):
        raise TraceError("unknown start kind")
    header.start_kind = StartKind(start)

    return header


def _decode_bonus(raw: int, field_name: str) -> Bonus:
    if raw > int(Bonus.EARTH):
        raise TraceError(f"unknown bonus in start state field {field_name}")
    return Bonus(raw)


def _encode_start(start: StartState) -> bytes:
    w = _Writer()
    w.string(start.sector)
    w.string(start.spawnpoint)
    w.f32(start.position_x)
    w.f32(start.position_y)
    w.u8(1 if start.is_checkpoint else 0)
    w.f32(start.play_time)
    w.u32(start.coins)
    w.u32(start.tuxdolls)
    w.u8(int(start.bonus))
    w.u8(int(start.item_pocket))
    w.u32(start.coins_at_start)
    w.u8(int(start.bonus_at_start))
    w.u8(int(start.pocket_at_start))
    return w.data()


def _decode_start(payload: bytes) -> StartState:
    r = _Reader(payload)
    start = StartState()
    start.sector = r.string()
    start.spawnpoint = r.string()
    start.position_x = r.f32()
    start.position_y = r.f32()

    # Not `!= 0`: a byte outside {0,1} means the producer and this reader
    # disagree about the record's shape, which is exactly what a boundary
    # check is for. Silently coercing it would hide the disagreement.
    checkpoint = r.u8()
    if checkpoint > 1:
        raise TraceError("start state is_checkpoint must be 0 or 1")
    start.is_checkpoint = checkpoint == 1

    start.play_time = r.f32()
    start.coins = r.u32()
    start.tuxdolls = r.u32()
    start.bonus = _decode_bonus(r.u8(), "bonus")
    start.item_pocket = _decode_bonus(r.u8(), "item_pocket")
    start.coins_at_start = r.u32()
    start.bonus_at_start = _decode_bonus(r.u8(), "bonus_at_start")
    start.pocket_at_start = _decode_bonus(r.u8(), "pocket_at_start")
    return start


def _encode_inputs(masks: List[int]) -> bytes:
    w = _Writer()
    i = 0
    while i < len(masks):
        mask = masks[i]
        if mask & RESERVED_MASK:
            raise TraceError("action mask sets a reserved bit")

        run = 1
        while (
            i + run < len(masks)
            and masks[i + run] == mask
            and run < MAX_RUN_LENGTH
        ):
            run += 1

        w.u16(mask)
        w.u16(run)
        i += run
    return w.data()


def _decode_inputs(payload: bytes, expected_steps: int) -> List[int]:
    r = _Reader(payload)
    masks: List[int] = []
    # Running total, checked *inside* the loop. Each 4-byte entry expands to up
    # to 65535 masks, so checking only the final total would let a ~50 KB file
    # materialize ~10 GB of list before any bound applies. Mirrors the same
    # guard in InputTrack::decode on the C++ side.
    total = 0
    while not r.exhausted:
        mask = r.u16()
        run = r.u16()
        if run == 0:
            raise TraceError("input run length must be at least 1")
        if mask & RESERVED_MASK:
            raise TraceError("action mask sets a reserved bit")
        total += run
        if total > expected_steps:
            raise TraceError("input run lengths exceed the header step count")
        masks.extend([mask] * run)

    if len(masks) != expected_steps:
        raise TraceError("input step count disagrees with the header")
    return masks


def _encode_strings(values: List[str]) -> bytes:
    if len(values) > MAX_STRING_TABLE:
        raise TraceError("string table is full")
    w = _Writer()
    w.u16(len(values))
    for value in values:
        w.string(value)
    return w.data()


def _decode_strings(payload: bytes) -> List[str]:
    r = _Reader(payload)
    count = r.u16()
    values = [r.string() for _ in range(count)]
    if len(set(values)) != len(values):
        raise TraceError("duplicate entry in string table")
    return values


def _encode_visual(frames: List[VisualFrame]) -> bytes:
    w = _Writer()
    for frame in frames:
        w.f32(frame.pos_x)
        w.f32(frame.pos_y)
        w.f32(frame.vel_x)
        w.f32(frame.vel_y)
        w.u16(frame.action_id)
        w.u8(frame.frame_idx)
        w.u8(frame.frame_progress)
        w.u8(frame.flags)
    return w.data()


def _decode_visual(payload: bytes, expected_steps: int) -> List[VisualFrame]:
    if len(payload) % VISUAL_FRAME_BYTES:
        raise TraceError("visual payload is not a whole number of frames")

    r = _Reader(payload)
    frames: List[VisualFrame] = []
    while not r.exhausted:
        frames.append(
            VisualFrame(
                pos_x=r.f32(),
                pos_y=r.f32(),
                vel_x=r.f32(),
                vel_y=r.f32(),
                action_id=r.u16(),
                frame_idx=r.u8(),
                frame_progress=r.u8(),
                flags=r.u8(),
            )
        )

    if len(frames) != expected_steps:
        raise TraceError("visual step count disagrees with the header")
    return frames


def _encode_transitions(transitions: List[Tuple[int, int]]) -> bytes:
    w = _Writer()
    for sector_id, first_step in transitions:
        w.u16(sector_id)
        w.u32(first_step)
    return w.data()


def _decode_transitions(payload: bytes) -> List[Tuple[int, int]]:
    if len(payload) % 6:
        raise TraceError("transition payload is not a whole number of entries")
    r = _Reader(payload)
    out: List[Tuple[int, int]] = []
    while not r.exhausted:
        out.append((r.u16(), r.u32()))
    return out


def _encode_notes(episode: bytes, steps: List[Tuple[int, bytes]]) -> bytes:
    w = _Writer()
    w.u32(len(episode))
    w.raw(episode)
    for step, data in steps:
        w.u32(step)
        w.u32(len(data))
        w.raw(data)
    return w.data()


def _decode_notes(payload: bytes) -> Tuple[bytes, List[Tuple[int, bytes]]]:
    if not payload:
        return b"", []

    r = _Reader(payload)
    episode_len = r.u32()
    if episode_len > r.remaining:
        raise TraceError("note episode blob is truncated")
    episode = r.raw(episode_len)

    steps: List[Tuple[int, bytes]] = []
    while not r.exhausted:
        step = r.u32()
        length = r.u32()
        if length > r.remaining:
            raise TraceError("note step blob is truncated")
        steps.append((step, r.raw(length)))
    return episode, steps


def _write_chunk(w: _Writer, name: str, payload: bytes) -> None:
    if not name:
        raise TraceError("chunk name must not be empty")
    if not name.isascii():
        raise TraceError("chunk name must be ASCII")
    w.string(name)
    w.u32(len(payload))
    w.raw(payload)


def _read_chunks(payload: bytes) -> List[Tuple[str, bytes]]:
    r = _Reader(payload)
    chunks: List[Tuple[str, bytes]] = []
    # Duplicate names are rejected rather than resolved. The dict() below used
    # to keep the last copy while C++'s find_chunk() keeps the first, so a file
    # with two "header" chunks decoded to two different traces -- and either
    # way one copy was silently discarded. No format-version bump: such a file
    # was never meaningfully valid.
    seen: set[str] = set()
    while not r.exhausted:
        name = r.string()
        if not name:
            raise TraceError("chunk name must not be empty")
        if not name.isascii():
            raise TraceError("chunk name must be ASCII")
        if name in seen:
            raise TraceError(f"duplicate chunk name: {name}")
        seen.add(name)
        length = r.u32()
        if length > r.remaining:
            raise TraceError("chunk payload is truncated")
        chunks.append((name, r.raw(length)))
    return chunks


def validate(trace: Trace) -> None:
    """Mirrors trace::validate in the C++ implementation."""
    # Mirrors the C++ rule in trace_file.cpp: a non-positive seed means "seed
    # from the wall clock" to the engine, so such a trace is not reproducible.
    if trace.header.seed <= 0:
        raise TraceError("header seed must be positive")

    if trace.header.viewport_width == 0 or trace.header.viewport_height == 0:
        raise TraceError("header viewport must be non-zero in both dimensions")

    peek = trace.header.camera_peek_multiplier
    if not math.isfinite(peek) or peek < 0.0:
        raise TraceError(
            "header camera_peek_multiplier must be finite and non-negative"
        )

    if not trace.header.step_rate > 0.0:
        raise TraceError("header step_rate must be positive")

    if len(trace.inputs) != trace.header.step_count:
        raise TraceError("input step count disagrees with the header")

    # Both directions. "start" is on KNOWN_CHUNKS, so a file carrying it under
    # an UNKNOWN kind would lose it silently on a round-trip rather than being
    # preserved as an unknown chunk -- the same reasoning that makes
    # "companions without visual" an error rather than a shrug.
    capturable = trace.header.start_kind != StartKind.UNKNOWN
    if capturable and trace.start is None:
        raise TraceError("this start kind requires a start chunk")
    if not capturable and trace.start is not None:
        raise TraceError("an unknown start kind cannot carry a start chunk")

    if trace.start is not None:
        if not trace.start.sector:
            raise TraceError("start state sector must not be empty")

        if trace.start.is_checkpoint != (
            trace.header.start_kind == StartKind.CHECKPOINT
        ):
            raise TraceError(
                "start state checkpoint flag disagrees with start_kind"
            )

        # game_session.hpp:63 -- "If a spawnpoint is set, the spawn position
        # shall not, and vice versa."
        if trace.start.spawnpoint and (
            trace.start.position_x != 0.0 or trace.start.position_y != 0.0
        ):
            raise TraceError("start state carries both a spawnpoint and a position")

        if (
            not math.isfinite(trace.start.play_time)
            or trace.start.play_time < 0.0
        ):
            raise TraceError(
                "start state play_time must be finite and non-negative"
            )

    if not trace.visual:
        if trace.sector_transitions:
            raise TraceError("sector transitions require a visual track")

        return

    if len(trace.visual) != trace.header.step_count:
        raise TraceError("visual step count disagrees with the header")

    # The intern tables are written as part of the file whenever there is a
    # visual track, and _decode_strings rejects duplicates -- so without these
    # two checks dumps() succeeds and loads() on its own output raises. C++
    # cannot reach that state at all: StringTable dedups on intern().
    if len(set(trace.actions)) != len(trace.actions):
        raise TraceError("duplicate entry in string table")
    if len(set(trace.sectors)) != len(trace.sectors):
        raise TraceError("duplicate entry in string table")

    for frame in trace.visual:
        if frame.action_id >= len(trace.actions):
            raise TraceError("visual frame references an unknown action id")

    if not trace.sector_transitions:
        raise TraceError("a visual track requires at least one sector transition")

    if trace.sector_transitions[0][1] != 0:
        raise TraceError("the first sector transition must be at step 0")

    previous = None
    for sector_id, first_step in trace.sector_transitions:
        if previous is not None and first_step <= previous:
            raise TraceError("sector transitions must strictly increase")
        if first_step >= trace.header.step_count:
            raise TraceError("sector transition starts past the end of the trace")
        if sector_id >= len(trace.sectors):
            raise TraceError("sector transition references an unknown sector id")
        previous = first_step


def dumps(trace: Trace) -> bytes:
    validate(trace)

    chunks = _Writer()
    _write_chunk(chunks, "header", _encode_header(trace.header))
    _write_chunk(chunks, "inputs", _encode_inputs(trace.inputs))

    if trace.start is not None:
        _write_chunk(chunks, "start", _encode_start(trace.start))

    if trace.visual:
        _write_chunk(chunks, "actions", _encode_strings(trace.actions))
        _write_chunk(chunks, "sectors", _encode_strings(trace.sectors))
        _write_chunk(
            chunks, "transitions", _encode_transitions(trace.sector_transitions)
        )
        _write_chunk(chunks, "visual", _encode_visual(trace.visual))

    if trace.episode_note or trace.step_notes:
        _write_chunk(
            chunks, "notes", _encode_notes(trace.episode_note, trace.step_notes)
        )

    # A caller-supplied unknown chunk must not collide with a name dumps()
    # writes itself, nor with another unknown chunk: _read_chunks rejects
    # duplicate names, so emitting two "header" chunks would produce a file
    # this module cannot read back. Fail at write time instead.
    written = set(KNOWN_CHUNKS)
    for name, payload in trace.unknown_chunks:
        if name in written:
            raise TraceError(f"unknown chunk collides with another chunk name: {name}")
        written.add(name)
        _write_chunk(chunks, name, payload)

    raw = chunks.data()
    out = _Writer()
    out.raw(MAGIC)
    out.u16(FORMAT_VERSION)
    out.u32(len(raw))
    out.raw(zlib.compress(raw, 9))
    return out.data()


def loads(data: bytes) -> Trace:
    if len(data) < 10:
        raise TraceError("file is shorter than the preamble")

    if data[:4] != MAGIC:
        raise TraceError("not a SuperTux trace file")

    version = struct.unpack_from("<H", data, 4)[0]
    if version != FORMAT_VERSION:
        raise TraceError(f"unsupported trace format version {version}")

    inflated_size = struct.unpack_from("<I", data, 6)[0]

    # Bound the declared size before decompressing, exactly as C++ does. An
    # unbounded zlib.decompress() here turned a 204 KB file into 410 MB of
    # RSS; the ratio bound rejects it from the preamble alone.
    compressed_bytes = len(data) - 10
    if inflated_size // MAX_DEFLATE_EXPANSION_RATIO > compressed_bytes:
        raise TraceError("declared inflated size is impossible for this stream")
    if inflated_size > MAX_INFLATED_BYTES:
        raise TraceError("declared inflated size exceeds the maximum trace size")

    # Cap the output at inflated_size + 1 so an over-producing stream is
    # detected without materializing it: one extra byte is enough to know the
    # declared size was wrong, and unconsumed_tail non-empty means the stream
    # still had more to give.
    try:
        decompressor = zlib.decompressobj()
        raw = decompressor.decompress(data[10:], max_length=inflated_size + 1)
    except zlib.error as exc:
        raise TraceError(f"zlib decompression failed: {exc}") from exc

    if len(raw) != inflated_size or decompressor.unconsumed_tail:
        raise TraceError("inflated size disagrees with the preamble")

    # Unlike the one-shot zlib.decompress(), a decompressobj returns what it
    # has for a truncated stream instead of raising, so the truncation has to
    # be checked explicitly. C++'s uncompress() reports it as an error.
    if not decompressor.eof:
        raise TraceError("zlib decompression failed: truncated stream")

    chunks = _read_chunks(raw)
    by_name = dict(chunks)

    if "header" not in by_name:
        raise TraceError("trace has no header chunk")
    if "inputs" not in by_name:
        raise TraceError("trace has no inputs chunk")

    trace = Trace()
    trace.header = _decode_header(by_name["header"])
    trace.inputs = _decode_inputs(by_name["inputs"], trace.header.step_count)

    if "start" in by_name:
        trace.start = _decode_start(by_name["start"])

    # Companions without "visual" are rejected, not dropped: they are all on
    # KNOWN_CHUNKS, so they are not preserved as unknown chunks either, and a
    # third-party file carrying them would lose them silently on re-write.
    if "visual" not in by_name:
        for orphan in ("actions", "sectors", "transitions"):
            if orphan in by_name:
                raise TraceError(
                    "actions, sectors and transitions require a visual chunk"
                )

    if "visual" in by_name:
        for required in ("actions", "sectors", "transitions"):
            if required not in by_name:
                raise TraceError(
                    "a visual chunk requires actions, sectors and transitions"
                )
        # A present-but-empty visual chunk has no representation in this
        # Trace: `visual == []` means "no visual track" everywhere below, so
        # the four chunks would be silently dropped on re-write. C++ rejects
        # such a file, so this must too.
        if not by_name["visual"]:
            raise TraceError("a visual chunk must carry at least one frame")
        trace.actions = _decode_strings(by_name["actions"])
        trace.sectors = _decode_strings(by_name["sectors"])
        trace.sector_transitions = _decode_transitions(by_name["transitions"])
        trace.visual = _decode_visual(by_name["visual"], trace.header.step_count)

    if "notes" in by_name:
        trace.episode_note, trace.step_notes = _decode_notes(by_name["notes"])

    trace.unknown_chunks = [
        (name, payload) for name, payload in chunks if name not in KNOWN_CHUNKS
    ]

    validate(trace)
    return trace


def read_trace(path: str | Path) -> Trace:
    return loads(Path(path).read_bytes())


def write_trace(path: str | Path, trace: Trace) -> None:
    Path(path).write_bytes(dumps(trace))


def _parse_provenance_object(provenance: str) -> dict | None:
    """Parses provenance as a JSON object, or returns None on anything else
    -- unparseable text, a JSON value that is not an object, or an empty
    string. Provenance is opaque UTF-8 and a third-party producer may put
    anything there, so a parse failure is "no findings from this side",
    never an exception -- including a maliciously or accidentally deep
    structure. ``json.loads`` raises ``RecursionError`` (a ``RuntimeError``
    subclass, not caught by ``ValueError``/``TypeError``) on input like
    ``"[" * 60000``, and that path is reachable end to end: ``loads()``
    validates provenance as UTF-8 but not as JSON, so a third-party trace
    can carry exactly this, and both ``recorded_loop()`` and ``findings()``
    -- below and via ``_main`` -- must fail closed rather than crash.

    Shared by ``recorded_loop()`` and ``findings()`` so this hole (and any
    future one in how a malformed ``provenance`` string is parsed) has
    exactly one place to close, rather than a fork of the same try/except
    that could drift.
    """
    if not provenance:
        return None
    try:
        parsed = json.loads(provenance)
    except (ValueError, TypeError, RecursionError):
        return None
    return parsed if isinstance(parsed, dict) else None


def recorded_loop(trace: Trace) -> str | None:
    """Which loop recorded this trace: "step-driven", "wall-clock", or None
    when the provenance says nothing.

    None covers three separate cases on purpose, because a consumer cannot
    tell them apart and must not guess: a trace written before the ``loop``
    marker existed, a third-party trace that does not follow the convention,
    and a producer whose provenance is not a JSON object at all.

    This lives here and NOT in the engine on purpose. ``provenance`` is
    opaque to SuperTux by design -- the engine copies the bytes and never
    looks inside -- so an in-engine consumer cannot refuse or warn on this
    without doing exactly what that design forbids. A typed header field
    would allow a structural refusal and is a format-v3 decision. Until
    then, a provenance-aware reader out here is where the check belongs.

    Why anyone cares: handle_screen_switch() runs once per frame under the
    human loop and once per step under run_steps, so a wall-clock recording
    can diverge on replay wherever a screen switch lands mid-frame. A second
    divergence has the same source: GameSession::setup() runs at a clock
    earlier than game_time_origin in a wall-clock recording and at the
    origin on replay. Both are exactly zero for a --steps recording.

    Never raises. A producer may put anything in provenance, and a reader
    that throws on free-form bytes would make an opaque field not opaque.
    """
    parsed = _parse_provenance_object(trace.header.provenance)
    if parsed is None:
        return None
    loop = parsed.get("loop")
    return loop if isinstance(loop, str) else None


def warn_if_wall_clock(trace: Trace) -> str | None:
    """The warning text for a wall-clock-recorded trace, or None.

    Returned rather than printed, so a caller decides where it goes.
    """
    if recorded_loop(trace) != "wall-clock":
        return None
    return (
        "this trace was recorded under the wall-clock loop; replaying it "
        "under the step-driven loop can diverge where a screen switch lands "
        "and where setup()'s clock origin falls. Re-record under --steps to "
        "avoid both."
    )


# --- compare(): the out-of-engine face of the P3e-1 comparison contract ----
#
# This mirrors src/trace/trace_compare.cpp field for field, name for name,
# and in the same emission order, because a later golden corpus of trace
# pairs pins both implementations against each other and must get the same
# verdict from both. See the spec's "Comparison and preconditions" section
# for the three-tier contract this walks, and trace_compare.cpp's top-level
# compare() for the order this one is built to match line for line.


def _same_bits(a: float, b: float) -> bool:
    """Float equality by bit pattern -- the Python side of the same rule
    trace_compare.cpp's same_bits() enforces. `==` is wrong twice over:
    NaN != NaN would report two identical traces as divergent, and
    -0.0 == 0.0 would report two different ones as equal. The determinism
    claim is byte-identical output, so bits are what it means."""
    return struct.pack("<f", a) == struct.pack("<f", b)


def _render(value) -> str:
    """Renders a value for a Difference's expected/actual text. Deliberately
    NOT designed to match trace_compare.cpp's rendering byte for byte -- the
    two languages format floats differently and always will, and the spec
    says explicitly that rendered strings are not compared across languages.
    Only kind, field and step are pinned by the corpus."""
    if isinstance(value, bool):
        return "true" if value else "false"
    if isinstance(value, IntEnum):
        return f"{int(value)} ({value.name})"
    if isinstance(value, (bytes, bytearray)):
        return value.hex()
    return str(value)


def _describe_mask(mask: int) -> str:
    """"RIGHT|JUMP" for a mask, "-" for zero -- the Python twin of
    trace_compare.cpp's describe_mask(). Reserved bits render rather than
    vanish: a mask that should never reach a valid trace must stay visible
    if one somehow does."""
    if mask == 0:
        return "-"
    parts = [action.name for action in Action if mask & (1 << int(action))]
    reserved = mask & RESERVED_MASK
    if reserved:
        parts.append(f"reserved:0x{reserved:x}")
    return "|".join(parts)


@dataclass(frozen=True)
class Difference:
    kind: str  # "header" | "presence" | "inputs" | "visual" | "start" | "transitions"
    field: str
    step: int | None
    expected: str
    actual: str

    def describe(self) -> str:
        """"[step|transition] N field: expected vs actual", the position
        prefix omitted entirely when there is no step. Mirrors
        trace_compare.cpp's Difference::describe(): TRANSITIONS carries an
        index into sector_transitions, not a logical step, so it renders as
        "transition N" rather than "step N"."""
        prefix = ""
        if self.step is not None:
            word = "transition" if self.kind == "transitions" else "step"
            prefix = f"{word} {self.step} "
        return f"{prefix}{self.field}: {self.expected} vs {self.actual}"


@dataclass(frozen=True)
class Finding:
    kind: str  # "wall_clock_source" | "tainted_replay" | "replay_of_replay"
    detail: str


def _push(diffs: List[Difference], kind: str, field_name: str,
          expected: str, actual: str, step: int | None = None) -> None:
    diffs.append(Difference(kind=kind, field=field_name, step=step,
                             expected=expected, actual=actual))


def _compare_header(diffs: List[Difference], expected: Header, actual: Header) -> None:
    """Walks Header's tier-1 fields in declaration order, skipping the
    tier-2 fields with a one-line comment at the point each is skipped, the
    same discipline trace_compare.cpp's compare_header() uses."""
    # engine_version -- tier 2: a configure-time string, stale by
    # construction; it is a tier-3 precondition against this build instead.

    # platform -- tier 2: a replay runs on the machine doing the verifying,
    # so this field is a fact about *this* build on both sides of a round
    # trip; it is a tier-3 precondition instead.

    # level_path -- tier 2: an absolute local path that does not survive a
    # move between machines or checkouts. Level identity is level_md5, below.

    if expected.level_md5 != actual.level_md5:
        _push(diffs, "header", "level_md5",
              _render(expected.level_md5), _render(actual.level_md5))

    if expected.seed != actual.seed:
        _push(diffs, "header", "seed", _render(expected.seed), _render(actual.seed))

    if not _same_bits(expected.game_time_origin, actual.game_time_origin):
        _push(diffs, "header", "game_time_origin",
              _render(expected.game_time_origin), _render(actual.game_time_origin))

    if not _same_bits(expected.step_rate, actual.step_rate):
        _push(diffs, "header", "step_rate",
              _render(expected.step_rate), _render(actual.step_rate))

    if expected.step_count != actual.step_count:
        _push(diffs, "header", "step_count",
              _render(expected.step_count), _render(actual.step_count))

    if not _same_bits(expected.play_time, actual.play_time):
        _push(diffs, "header", "play_time",
              _render(expected.play_time), _render(actual.play_time))

    if expected.producer_kind != actual.producer_kind:
        _push(diffs, "header", "producer_kind",
              _render(expected.producer_kind), _render(actual.producer_kind))

    # provenance -- tier 2: differs by construction on every correct
    # replay -- it is what records that the run *was* a replay. The
    # out-of-engine face reads it for taint and loop findings instead; see
    # findings() below.

    if expected.outcome != actual.outcome:
        _push(diffs, "header", "outcome",
              _render(expected.outcome), _render(actual.outcome))

    if expected.deaths != actual.deaths:
        _push(diffs, "header", "deaths",
              _render(expected.deaths), _render(actual.deaths))

    if expected.coins != actual.coins:
        _push(diffs, "header", "coins",
              _render(expected.coins), _render(actual.coins))

    if expected.viewport_width != actual.viewport_width:
        _push(diffs, "header", "viewport_width",
              _render(expected.viewport_width), _render(actual.viewport_width))

    if expected.viewport_height != actual.viewport_height:
        _push(diffs, "header", "viewport_height",
              _render(expected.viewport_height), _render(actual.viewport_height))

    if expected.screen_shake_mode != actual.screen_shake_mode:
        _push(diffs, "header", "screen_shake_mode",
              _render(expected.screen_shake_mode), _render(actual.screen_shake_mode))

    if not _same_bits(expected.camera_peek_multiplier, actual.camera_peek_multiplier):
        _push(diffs, "header", "camera_peek_multiplier",
              _render(expected.camera_peek_multiplier),
              _render(actual.camera_peek_multiplier))

    if expected.start_kind != actual.start_kind:
        _push(diffs, "header", "start_kind",
              _render(expected.start_kind), _render(actual.start_kind))


def _compare_inputs(diffs: List[Difference], expected: List[int], actual: List[int]) -> None:
    """Walks the shared prefix of both input tracks (the length mismatch
    itself, if any, was already reported by compare() as inputs.length) and
    reports only the first differing step -- a divergent replay differs at
    every subsequent step, and reporting them all would bury the one that
    matters."""
    count = min(len(expected), len(actual))
    for step in range(count):
        if expected[step] != actual[step]:
            _push(diffs, "inputs", "inputs",
                  _describe_mask(expected[step]), _describe_mask(actual[step]),
                  step=step)
            break


def _compare_visual(diffs: List[Difference], expected: Trace, actual: Trace) -> None:
    """Walks the visual track's shared prefix for the first differing step,
    but reports *every* field that differs at that step rather than just the
    first -- a single mis-sampled step tends to move more than one field, and
    a caller debugging a divergence wants to know which.

    action_id is resolved through each trace's own actions table before
    comparison: two traces may intern the same strings under different ids,
    so comparing the raw id would be wrong in both directions -- see the
    spec's note on interned tables."""
    count = min(len(expected.visual), len(actual.visual))
    for step in range(count):
        ef = expected.visual[step]
        af = actual.visual[step]
        before = len(diffs)

        if not _same_bits(ef.pos_x, af.pos_x):
            _push(diffs, "visual", "visual.pos_x",
                  _render(ef.pos_x), _render(af.pos_x), step=step)

        if not _same_bits(ef.pos_y, af.pos_y):
            _push(diffs, "visual", "visual.pos_y",
                  _render(ef.pos_y), _render(af.pos_y), step=step)

        if not _same_bits(ef.vel_x, af.vel_x):
            _push(diffs, "visual", "visual.vel_x",
                  _render(ef.vel_x), _render(af.vel_x), step=step)

        if not _same_bits(ef.vel_y, af.vel_y):
            _push(diffs, "visual", "visual.vel_y",
                  _render(ef.vel_y), _render(af.vel_y), step=step)

        e_action = expected.actions[ef.action_id]
        a_action = actual.actions[af.action_id]
        if e_action != a_action:
            _push(diffs, "visual", "visual.action", e_action, a_action, step=step)

        if ef.frame_idx != af.frame_idx:
            _push(diffs, "visual", "visual.frame_idx",
                  _render(ef.frame_idx), _render(af.frame_idx), step=step)

        if ef.frame_progress != af.frame_progress:
            _push(diffs, "visual", "visual.frame_progress",
                  _render(ef.frame_progress), _render(af.frame_progress), step=step)

        if ef.flags != af.flags:
            _push(diffs, "visual", "visual.flags",
                  _render(ef.flags), _render(af.flags), step=step)

        if len(diffs) > before:
            break  # first differing step only, but every differing field at it


def _render_transition(sectors: List[str], transition: Tuple[int, int]) -> str:
    """Renders one transition as "<sector>@<first_step>", resolving
    sector_id through the table the transition's own trace carries -- the
    same resolve-before-compare rule as visual.action, for the same reason."""
    sector_id, first_step = transition
    return f"{sectors[sector_id]}@{first_step}"


def _compare_transitions(diffs: List[Difference], expected: Trace, actual: Trace) -> None:
    """Sector transitions compare like a track: length first (reported as
    "transitions.length" since sector_transitions is not step-indexed the
    way inputs/visual are), then the shared prefix walked for the first
    differing entry."""
    if len(expected.sector_transitions) != len(actual.sector_transitions):
        _push(diffs, "transitions", "transitions.length",
              _render(len(expected.sector_transitions)),
              _render(len(actual.sector_transitions)))

    count = min(len(expected.sector_transitions), len(actual.sector_transitions))
    for i in range(count):
        e_rendered = _render_transition(expected.sectors, expected.sector_transitions[i])
        a_rendered = _render_transition(actual.sectors, actual.sector_transitions[i])
        if e_rendered != a_rendered:
            _push(diffs, "transitions", "transitions", e_rendered, a_rendered, step=i)
            break


def _compare_start(diffs: List[Difference], expected: StartState, actual: StartState) -> None:
    """Every StartState field, in declaration order. Thirteen fields; a
    field added there without a line here is a field this comparator
    silently does not compare. Unlike inputs/visual this is not a per-step
    track, so every differing field is reported, not just the first."""
    if expected.sector != actual.sector:
        _push(diffs, "start", "start.sector", expected.sector, actual.sector)

    if expected.spawnpoint != actual.spawnpoint:
        _push(diffs, "start", "start.spawnpoint", expected.spawnpoint, actual.spawnpoint)

    if not _same_bits(expected.position_x, actual.position_x):
        _push(diffs, "start", "start.position_x",
              _render(expected.position_x), _render(actual.position_x))

    if not _same_bits(expected.position_y, actual.position_y):
        _push(diffs, "start", "start.position_y",
              _render(expected.position_y), _render(actual.position_y))

    if expected.is_checkpoint != actual.is_checkpoint:
        _push(diffs, "start", "start.is_checkpoint",
              _render(expected.is_checkpoint), _render(actual.is_checkpoint))

    if not _same_bits(expected.play_time, actual.play_time):
        _push(diffs, "start", "start.play_time",
              _render(expected.play_time), _render(actual.play_time))

    if expected.coins != actual.coins:
        _push(diffs, "start", "start.coins",
              _render(expected.coins), _render(actual.coins))

    if expected.tuxdolls != actual.tuxdolls:
        _push(diffs, "start", "start.tuxdolls",
              _render(expected.tuxdolls), _render(actual.tuxdolls))

    if expected.bonus != actual.bonus:
        _push(diffs, "start", "start.bonus",
              _render(expected.bonus), _render(actual.bonus))

    if expected.item_pocket != actual.item_pocket:
        _push(diffs, "start", "start.item_pocket",
              _render(expected.item_pocket), _render(actual.item_pocket))

    if expected.coins_at_start != actual.coins_at_start:
        _push(diffs, "start", "start.coins_at_start",
              _render(expected.coins_at_start), _render(actual.coins_at_start))

    if expected.bonus_at_start != actual.bonus_at_start:
        _push(diffs, "start", "start.bonus_at_start",
              _render(expected.bonus_at_start), _render(actual.bonus_at_start))

    if expected.pocket_at_start != actual.pocket_at_start:
        _push(diffs, "start", "start.pocket_at_start",
              _render(expected.pocket_at_start), _render(actual.pocket_at_start))


def compare(expected: Trace, actual: Trace) -> List[Difference]:
    """Compares two decoded traces under the spec's three-tier contract.
    Empty result means every tier-1 field is equal.

    has_visual and has_start are structural flags on the C++ Trace struct,
    but this dataclass carries no such booleans -- there is only the
    `visual` list and the `start` value. This is a real asymmetry between
    the two implementations, not an incidental one: presence is DERIVED
    here as bool(trace.visual) and trace.start is not None, rather than
    read off a stored flag the way the C++ side does.
    """
    diffs: List[Difference] = []

    # Presence is compared before content: a source carrying a track whose
    # replay carries none has diverged in the most consequential way
    # available, and reporting it as a length or content mismatch would bury
    # it. No early return here -- a caller wants every difference, not just
    # the first, so the header and track-length checks below still run even
    # when a presence flag disagrees.
    expected_has_visual = bool(expected.visual)
    actual_has_visual = bool(actual.visual)
    if expected_has_visual != actual_has_visual:
        _push(diffs, "presence", "has_visual",
              _render(expected_has_visual), _render(actual_has_visual))

    expected_has_start = expected.start is not None
    actual_has_start = actual.start is not None
    if expected_has_start != actual_has_start:
        _push(diffs, "presence", "has_start",
              _render(expected_has_start), _render(actual_has_start))

    _compare_header(diffs, expected.header, actual.header)

    # inputs is the ground truth track and is always present regardless of
    # has_visual; its length is a fact distinct from header.step_count,
    # which is only the source's own declared count. Reported as
    # "inputs.length" rather than "step_count" again, so a header
    # disagreement and a track that is actually a different length are two
    # facts, not one repeated. The walk itself still runs over the shared
    # prefix regardless of a length mismatch.
    if len(expected.inputs) != len(actual.inputs):
        _push(diffs, "inputs", "inputs.length",
              _render(len(expected.inputs)), _render(len(actual.inputs)))

    _compare_inputs(diffs, expected.inputs, actual.inputs)

    # visual, actions/sectors and sector_transitions all only mean something
    # when both sides claim to carry a visual track at all; a has_visual
    # mismatch above already reported the more consequential fact.
    if expected_has_visual and actual_has_visual:
        if len(expected.visual) != len(actual.visual):
            _push(diffs, "visual", "visual.length",
                  _render(len(expected.visual)), _render(len(actual.visual)))

        _compare_visual(diffs, expected, actual)
        _compare_transitions(diffs, expected, actual)

    # Likewise the start chunk only means something when both sides carry
    # one; has_start's own mismatch above already reported the more
    # consequential fact. Narrowed here as `is not None` on both sides
    # (rather than through the expected_has_start/actual_has_start booleans
    # computed above) so a type checker can see start is StartState, not
    # StartState | None, going into _compare_start.
    if expected.start is not None and actual.start is not None:
        _compare_start(diffs, expected.start, actual.start)

    return diffs


def findings(source: Trace, replay: Trace) -> List[Finding]:
    """Reads provenance for facts a comparison must not report as
    divergences -- reading it at all is the one thing this out-of-engine
    face can do that the engine face cannot (desideratum 13 forbids the
    engine to look inside provenance). None of the three findings below
    means the two traces differ, so none of them is a Difference."""
    out: List[Finding] = []

    source_provenance = _parse_provenance_object(source.header.provenance)
    replay_provenance = _parse_provenance_object(replay.header.provenance)

    if source_provenance is not None and source_provenance.get("loop") == "wall-clock":
        out.append(Finding(
            kind="wall_clock_source",
            detail=(
                "the source was recorded under the wall-clock loop; a "
                "replay under the step-driven loop can diverge where a "
                "screen switch lands and where setup()'s clock origin falls"
            ),
        ))

    if replay_provenance is not None and (
        replay_provenance.get("env_mismatch") or replay_provenance.get("trace_mismatch")
    ):
        out.append(Finding(
            kind="tainted_replay",
            detail="the replay's provenance carries env_mismatch or trace_mismatch",
        ))

    if source_provenance is not None and source_provenance.get("producer") == "executor":
        out.append(Finding(
            kind="replay_of_replay",
            detail="the source's own provenance says its producer was the executor",
        ))

    return out


def _main(argv: List[str]) -> int:
    parser = argparse.ArgumentParser(prog="supertux_trace")
    sub = parser.add_subparsers(dest="command", required=True)
    cmp_parser = sub.add_parser(
        "compare", help="compare two traces under the spec's tier contract")
    cmp_parser.add_argument("expected")
    cmp_parser.add_argument("actual")
    args = parser.parse_args(argv)

    expected, actual = read_trace(args.expected), read_trace(args.actual)
    for finding in findings(expected, actual):
        print(f"finding: {finding.kind}: {finding.detail}")
    diffs = compare(expected, actual)
    for d in diffs:
        print(d.describe())
    if not diffs:
        print("traces are equal under the tier-1 contract")
    return 1 if diffs else 0


if __name__ == "__main__":
    raise SystemExit(_main(sys.argv[1:]))
