//  SuperTux
//  Copyright (C) 2026 Abdelrahman Madkour <abdelrahman.madkour@patronus.ai>
//
//  This program is free software: you can redistribute it and/or modify
//  it under the terms of the GNU General Public License as published by
//  the Free Software Foundation, either version 3 of the License, or
//  (at your option) any later version.
//
//  This program is distributed in the hope that it will be useful,
//  but WITHOUT ANY WARRANTY; without even the implied warranty of
//  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//  GNU General Public License for more details.
//
//  You should have received a copy of the GNU General Public License
//  along with this program.  If not, see <http://www.gnu.org/licenses/>.

#include <fstream>
#include "trace/trace_file.hpp"

#include <array>
#include <cmath>
#include <cstring>
#include <new>
#include <set>
#include <string>

#include <zlib.h>

#include "trace/trace_bytes.hpp"
#include "trace/trace_error.hpp"

namespace trace {

namespace {

constexpr size_t TRANSITION_BYTES = 6;
constexpr size_t PREAMBLE_BYTES = 10;

/** zlib's worst-case expansion ratio for a valid deflate stream. Used to
    bound a declared inflated_size before allocating for it, so a hostile
    tiny file cannot claim a multi-gigabyte inflated size. */
constexpr size_t MAX_DEFLATE_EXPANSION_RATIO = 1032;

/** Absolute ceiling on a declared inflated_size, 256 MiB.

    The ratio bound above is not enough on its own: once the compressed body
    exceeds ~4.2 MB it permits the whole uint32 range, so a file could
    legitimately ask for a 4 GiB allocation and get std::bad_alloc on a
    constrained or 32-bit host -- an exception that is not ParseError.

    256 MiB is far above any real trace. A trace costs 21 bytes/step for the
    visual track plus at most 4 bytes/step for un-runnable inputs, so 256 MiB
    is over 10 million steps: about 43 hours of play at the engine's ~66.7
    steps/second. A file declaring more than that is either corrupt or
    hostile. */
constexpr uint32_t MAX_INFLATED_BYTES = 256u * 1024u * 1024u;

constexpr std::array<const char*, 8> KNOWN_CHUNKS = {
  "header", "inputs", "start", "actions", "sectors", "transitions", "visual",
  "notes"
};

bool is_known_chunk(const std::string& name)
{
  for (const char* known : KNOWN_CHUNKS)
  {
    if (name == known)
      return true;
  }
  return false;
}

} // namespace

std::vector<uint8_t>
encode_transitions(const std::vector<SectorTransition>& transitions)
{
  ByteWriter writer;
  for (const SectorTransition& transition : transitions)
  {
    writer.u16(transition.sector_id);
    writer.u32(transition.first_step);
  }
  return writer.data();
}

std::vector<SectorTransition>
decode_transitions(ByteReader& reader)
{
  if (reader.remaining() % TRANSITION_BYTES != 0)
    throw ParseError("transition payload is not a whole number of entries");

  std::vector<SectorTransition> out;
  while (!reader.exhausted())
  {
    SectorTransition transition;
    transition.sector_id = reader.u16();
    transition.first_step = reader.u32();
    out.push_back(transition);
  }
  return out;
}

void
validate(const Trace& trace)
{
  // Random::seed treats SEED_FROM_CLOCK (-1) as "seed from the wall clock", and
  // a 0 is resolved to a clock seed by main.cpp before any session starts. So a
  // non-positive seed here does not describe a reproducible episode -- it
  // describes a different one on every execution, silently. The engine cannot
  // produce such a header (src/supertux/main.cpp:1094-1095); a third-party
  // producer can.
  // See the spec's header-chunk section, and audit item 2.
  if (trace.header.seed <= 0)
    throw ParseError("header seed must be positive");

  if (trace.header.viewport_width == 0 || trace.header.viewport_height == 0)
    throw ParseError("header viewport must be non-zero in both dimensions");

  // Zero is a legitimate setting ("peeking moves the camera not at all"), so
  // only negative and non-finite values are refused.
  if (!std::isfinite(trace.header.camera_peek_multiplier) ||
      trace.header.camera_peek_multiplier < 0.0f)
  {
    throw ParseError("header camera_peek_multiplier must be finite and non-negative");
  }

  if (!(trace.header.step_rate > 0.0f))
    throw ParseError("header step_rate must be positive");

  if (trace.inputs.step_count() != trace.header.step_count)
    throw ParseError("input step count disagrees with the header");

  // Both directions. The `start` chunk is on the known-names list, so a file
  // carrying it under an UNKNOWN kind would lose it silently on a round-trip
  // rather than being preserved as an unknown chunk -- the same reasoning that
  // makes "companions without visual" an error rather than a shrug.
  const bool capturable = trace.header.start_kind != StartKind::UNKNOWN;
  if (capturable && !trace.has_start)
    throw ParseError("this start kind requires a start chunk");
  if (!capturable && trace.has_start)
    throw ParseError("an unknown start kind cannot carry a start chunk");

  if (trace.has_start)
  {
    if (trace.start.sector.empty())
      throw ParseError("start state sector must not be empty");

    if (trace.start.is_checkpoint !=
        (trace.header.start_kind == StartKind::CHECKPOINT))
    {
      throw ParseError("start state checkpoint flag disagrees with start_kind");
    }

    // game_session.hpp:63 -- "If a spawnpoint is set, the spawn position shall
    // not, and vice versa."
    if (!trace.start.spawnpoint.empty() &&
        (trace.start.position_x != 0.0f || trace.start.position_y != 0.0f))
    {
      throw ParseError("start state carries both a spawnpoint and a position");
    }

    if (!std::isfinite(trace.start.play_time) || trace.start.play_time < 0.0f)
      throw ParseError("start state play_time must be finite and non-negative");
  }

  if (!trace.has_visual)
  {
    if (!trace.sector_transitions.empty())
      throw ParseError("sector transitions require a visual track");

    return;
  }

  if (trace.visual.step_count() != trace.header.step_count)
    throw ParseError("visual step count disagrees with the header (container)");

  for (uint32_t i = 0; i < trace.visual.step_count(); ++i)
  {
    if (static_cast<size_t>(trace.visual.at(i).action_id) >= trace.actions.size())
      throw ParseError("visual frame references an unknown action id");
  }

  if (trace.sector_transitions.empty())
    throw ParseError("a visual track requires at least one sector transition");

  if (trace.sector_transitions.front().first_step != 0)
    throw ParseError("the first sector transition must be at step 0");

  for (size_t i = 0; i < trace.sector_transitions.size(); ++i)
  {
    const SectorTransition& transition = trace.sector_transitions[i];

    if (i > 0 &&
        transition.first_step <= trace.sector_transitions[i - 1].first_step)
    {
      throw ParseError("sector transitions must strictly increase");
    }

    if (transition.first_step >= trace.header.step_count)
      throw ParseError("sector transition starts past the end of the trace");

    if (static_cast<size_t>(transition.sector_id) >= trace.sectors.size())
      throw ParseError("sector transition references an unknown sector id");
  }
}

std::vector<uint8_t>
serialize(const Trace& trace)
{
  validate(trace);

  ByteWriter chunks;
  write_chunk(chunks, "header", trace.header.encode());
  write_chunk(chunks, "inputs", trace.inputs.encode());

  if (trace.has_start)
    write_chunk(chunks, "start", trace.start.encode());

  if (trace.has_visual)
  {
    write_chunk(chunks, "actions", trace.actions.encode());
    write_chunk(chunks, "sectors", trace.sectors.encode());
    write_chunk(chunks, "transitions", encode_transitions(trace.sector_transitions));
    write_chunk(chunks, "visual", trace.visual.encode());
  }

  if (!trace.notes.empty())
    write_chunk(chunks, "notes", trace.notes.encode());

  // A caller-supplied unknown chunk must not collide with a name serialize()
  // writes itself, nor with another unknown chunk: read_chunks() rejects
  // duplicate names, so emitting two "header" chunks would produce a file
  // this library cannot read back. Fail at write time instead.
  std::set<std::string> written(KNOWN_CHUNKS.begin(), KNOWN_CHUNKS.end());
  for (const Chunk& chunk : trace.unknown_chunks)
  {
    if (!written.insert(chunk.name).second)
      throw ParseError("unknown chunk collides with another chunk name: " + chunk.name);

    write_chunk(chunks, chunk.name, chunk.payload);
  }

  const std::vector<uint8_t>& raw = chunks.data();

  uLongf deflated_size = compressBound(static_cast<uLong>(raw.size()));
  std::vector<uint8_t> deflated(deflated_size);
  const int rc = compress2(deflated.data(), &deflated_size,
                           raw.data(), static_cast<uLong>(raw.size()),
                           Z_BEST_COMPRESSION);
  if (rc != Z_OK)
    throw ParseError("zlib compression failed");

  deflated.resize(deflated_size);

  ByteWriter out;
  out.bytes(reinterpret_cast<const uint8_t*>(MAGIC.data()), MAGIC.size());
  out.u16(FORMAT_VERSION);
  out.u32(static_cast<uint32_t>(raw.size()));
  out.bytes(deflated.data(), deflated.size());
  return out.data();
}

Trace
deserialize(const uint8_t* data, size_t size)
{
  if (size < PREAMBLE_BYTES)
    throw ParseError("file is shorter than the preamble");

  ByteReader preamble(data, PREAMBLE_BYTES);

  std::array<char, 4> magic = {};
  preamble.bytes(reinterpret_cast<uint8_t*>(magic.data()), magic.size());
  if (std::memcmp(magic.data(), MAGIC.data(), MAGIC.size()) != 0)
    throw ParseError("not a SuperTux trace file");

  const uint16_t version = preamble.u16();
  if (version != FORMAT_VERSION)
    throw ParseError("unsupported trace format version");

  const uint32_t inflated_size = preamble.u32();

  const size_t compressed_bytes = size - PREAMBLE_BYTES;
  if (inflated_size / MAX_DEFLATE_EXPANSION_RATIO > compressed_bytes)
    throw ParseError("declared inflated size is impossible for this stream");

  if (inflated_size > MAX_INFLATED_BYTES)
    throw ParseError("declared inflated size exceeds the maximum trace size");

  // Belt and braces: the two bounds above make a hostile allocation
  // implausible, but the "every decode path throws ParseError" contract must
  // hold even on a host that cannot satisfy a plausible request.
  std::vector<uint8_t> inflated;
  try
  {
    inflated.resize(inflated_size);
  }
  catch (const std::bad_alloc&)
  {
    throw ParseError("cannot allocate the declared inflated size");
  }

  uLongf produced = inflated_size;
  const int rc = uncompress(inflated.data(), &produced,
                            data + PREAMBLE_BYTES,
                            static_cast<uLong>(size - PREAMBLE_BYTES));
  if (rc != Z_OK)
    throw ParseError("zlib decompression failed");

  if (produced != inflated_size)
    throw ParseError("inflated size disagrees with the preamble");

  ByteReader chunk_reader(inflated.data(), inflated.size());
  const std::vector<Chunk> chunks = read_chunks(chunk_reader);

  const Chunk* header_chunk = find_chunk(chunks, "header");
  if (header_chunk == nullptr)
    throw ParseError("trace has no header chunk");

  Trace trace;
  {
    ByteReader reader(header_chunk->payload.data(), header_chunk->payload.size());
    trace.header = Header::decode(reader);
  }

  const Chunk* inputs_chunk = find_chunk(chunks, "inputs");
  if (inputs_chunk == nullptr)
    throw ParseError("trace has no inputs chunk");
  {
    ByteReader reader(inputs_chunk->payload.data(), inputs_chunk->payload.size());
    trace.inputs = InputTrack::decode(reader, trace.header.step_count);
  }

  const Chunk* start_chunk = find_chunk(chunks, "start");
  if (start_chunk != nullptr)
  {
    ByteReader reader(start_chunk->payload.data(), start_chunk->payload.size());
    trace.start = StartState::decode(reader);
    trace.has_start = true;
  }

  const Chunk* visual_chunk = find_chunk(chunks, "visual");
  const Chunk* actions_chunk = find_chunk(chunks, "actions");
  const Chunk* sectors_chunk = find_chunk(chunks, "sectors");
  const Chunk* transitions_chunk = find_chunk(chunks, "transitions");

  // The companion chunks are decoded only alongside "visual", and they are on
  // the known-names list so they are not preserved as unknown chunks either.
  // A third-party file carrying any of them without "visual" would therefore
  // lose them silently on a round-trip. Reject instead: "visual" without its
  // companions is already an error, so the reverse must be one too.
  if (visual_chunk == nullptr &&
      (actions_chunk != nullptr || sectors_chunk != nullptr ||
       transitions_chunk != nullptr))
  {
    throw ParseError("actions, sectors and transitions require a visual chunk");
  }

  if (visual_chunk != nullptr)
  {
    if (actions_chunk == nullptr || sectors_chunk == nullptr ||
        transitions_chunk == nullptr)
    {
      throw ParseError("a visual chunk requires actions, sectors and transitions");
    }

    // A present-but-empty visual chunk cannot be represented as "has a visual
    // track" on the Python side, whose Trace carries a plain list; rejecting
    // it here and there keeps the two implementations' notion of validity
    // identical. Neither writer emits one.
    if (visual_chunk->payload.empty())
      throw ParseError("a visual chunk must carry at least one frame");

    {
      ByteReader reader(actions_chunk->payload.data(),
                        actions_chunk->payload.size());
      trace.actions = StringTable::decode(reader);
    }
    {
      ByteReader reader(sectors_chunk->payload.data(), sectors_chunk->payload.size());
      trace.sectors = StringTable::decode(reader);
    }
    {
      ByteReader reader(transitions_chunk->payload.data(),
                        transitions_chunk->payload.size());
      trace.sector_transitions = decode_transitions(reader);
    }
    {
      ByteReader reader(visual_chunk->payload.data(), visual_chunk->payload.size());
      trace.visual = VisualTrack::decode(reader, trace.header.step_count);
    }
    trace.has_visual = true;
  }

  const Chunk* notes_chunk = find_chunk(chunks, "notes");
  if (notes_chunk != nullptr)
  {
    ByteReader reader(notes_chunk->payload.data(), notes_chunk->payload.size());
    trace.notes = NoteStore::decode(reader);
  }

  for (const Chunk& chunk : chunks)
  {
    if (!is_known_chunk(chunk.name))
      trace.unknown_chunks.push_back(chunk);
  }

  validate(trace);
  return trace;
}

namespace {

/** Upper bound on how much inflated output the fast header-only path below
    will ever produce before giving up and falling back to a full
    deserialize(). Deliberately generous relative to any real header --
    two version strings, a level path, a 16-byte digest, a handful of
    scalars, and a provenance string, well under a kilobyte in every
    fixture and probe this repository has produced -- so this only needs to
    be big enough that the header chunk always fits in practice, not tight
    against it. A header that does not fit just falls back; it is not
    treated as an error. */
constexpr size_t HEADER_ONLY_BUDGET = 64 * 1024;

/** Best-effort, bounded attempt at exactly what read_header() promises:
    decode only the "header" chunk, without inflating the rest of the
    stream. Returns false -- deliberately not a thrown ParseError -- for
    ANYTHING short of a complete header chunk within HEADER_ONLY_BUDGET of
    inflated output, so the caller can fall back to the always-correct
    deserialize() rather than this function having to tell a genuinely
    malformed file apart from "the header just didn't fit in the budget" or
    "this file is shaped some way the fast path does not specifically
    handle". Never allocates by a size this function does not itself
    choose (HEADER_ONLY_BUDGET): unlike deserialize(), it does not trust
    the preamble's declared inflated_size, because trusting it is exactly
    what would force inflating the whole stream to size the output buffer
    correctly. */
bool
try_read_header_only(const uint8_t* data, size_t size, Header& out)
{
  if (size < PREAMBLE_BYTES)
    return false;

  if (std::memcmp(data, MAGIC.data(), MAGIC.size()) != 0)
    return false;

  ByteReader preamble(data + MAGIC.size(), PREAMBLE_BYTES - MAGIC.size());
  const uint16_t version = preamble.u16();
  // The declared inflated_size (next 4 bytes) is read by deserialize(), not
  // by this function -- see this function's own header comment on why.
  if (version != FORMAT_VERSION)
    return false;

  const uint8_t* compressed = data + PREAMBLE_BYTES;
  const uInt compressed_size = static_cast<uInt>(size - PREAMBLE_BYTES);

  std::vector<uint8_t> inflated(HEADER_ONLY_BUDGET);

  z_stream strm{};
  if (inflateInit(&strm) != Z_OK)
    return false;

  strm.next_in = const_cast<Bytef*>(compressed);
  strm.avail_in = compressed_size;
  strm.next_out = inflated.data();
  strm.avail_out = static_cast<uInt>(inflated.size());

  // The whole compressed buffer is handed over as avail_in up front (not
  // fed in incrementally), so this loop can only end by filling avail_out
  // (budget reached, stream not necessarily finished -- rc stays Z_OK) or
  // by inflate() itself reporting the stream's end or an error. It cannot
  // spin: inflate() only returns Z_OK when it also, per its own contract,
  // has changed avail_in or avail_out (i.e. made progress), and avail_out
  // is strictly decreasing on every Z_OK iteration.
  int rc = Z_OK;
  while (strm.avail_out > 0 && rc == Z_OK)
    rc = inflate(&strm, Z_NO_FLUSH);

  const size_t produced = inflated.size() - strm.avail_out;
  inflateEnd(&strm);

  if (rc != Z_OK && rc != Z_STREAM_END)
    return false;

  // Parse exactly one chunk from the front of inflated[0..produced) -- the
  // same wire shape write_chunk()/read_chunks() use (trace_chunk.cpp): a
  // NUL-terminated name, a u32 length, then the payload. ByteReader::str()
  // and ::u32() throw ParseError on anything short of that being fully
  // present, which this function reinterprets as "budget exceeded, fall
  // back" rather than a genuine parse failure -- deserialize() is the one
  // place a real malformed-file ParseError is allowed to originate.
  try
  {
    ByteReader reader(inflated.data(), produced);
    const std::string name = reader.str();
    if (name != "header")
      return false; // not first -- never true of a file this library wrote

    const uint32_t length = reader.u32();
    if (length > reader.remaining())
      return false;

    ByteReader payload_reader(inflated.data() + reader.position(), length);
    out = Header::decode(payload_reader);
  }
  catch (const ParseError&)
  {
    return false;
  }

  return true;
}

} // namespace

Header
read_header(const uint8_t* data, size_t size)
{
  Header header;
  if (try_read_header_only(data, size, header))
    return header;

  // Guaranteed correct, if slower: every case try_read_header_only() gives
  // up on -- an oversized header, a corrupt file, anything shaped in a way
  // the fast path does not specifically handle -- is exactly a case where
  // "do what deserialize() would do" is the only honest answer. This is
  // also deserialize()'s own ParseError, unchanged, for a genuinely
  // malformed file: read_header() throws under exactly the same conditions
  // deserialize() does.
  return deserialize(data, size).header;
}

std::vector<uint8_t>
read_file_bytes(const std::string& path)
{
  std::ifstream in(path, std::ios::binary | std::ios::ate);
  if (!in)
    throw ParseError("cannot open trace file: " + path);

  const std::streamoff size = in.tellg();
  if (size < 0)
    throw ParseError("cannot determine the size of trace file: " + path);

  in.seekg(0, std::ios::beg);
  std::vector<uint8_t> bytes(static_cast<size_t>(size));
  if (size > 0)
  {
    in.read(reinterpret_cast<char*>(bytes.data()),
            static_cast<std::streamsize>(size));
    if (!in)
      throw ParseError("cannot read trace file: " + path);
  }
  return bytes;
}

} // namespace trace

/* EOF */
