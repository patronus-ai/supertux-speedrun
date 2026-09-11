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

#include "trace/trace_start.hpp"

#include "trace/trace_bytes.hpp"
#include "trace/trace_error.hpp"

namespace trace {

namespace {

Bonus decode_bonus(uint8_t raw, const char* field)
{
  if (raw > static_cast<uint8_t>(Bonus::EARTH))
    throw ParseError(std::string("unknown bonus in start state field ") + field);
  return static_cast<Bonus>(raw);
}

} // namespace

std::vector<uint8_t>
StartState::encode() const
{
  ByteWriter writer;
  writer.str(sector);
  writer.str(spawnpoint);
  writer.f32(position_x);
  writer.f32(position_y);
  writer.u8(is_checkpoint ? 1u : 0u);
  writer.f32(play_time);
  writer.u32(coins);
  writer.u32(tuxdolls);
  writer.u8(static_cast<uint8_t>(bonus));
  writer.u8(static_cast<uint8_t>(item_pocket));
  writer.u32(coins_at_start);
  writer.u8(static_cast<uint8_t>(bonus_at_start));
  writer.u8(static_cast<uint8_t>(pocket_at_start));
  return writer.data();
}

StartState
StartState::decode(ByteReader& reader)
{
  StartState state;
  state.sector = reader.str();
  state.spawnpoint = reader.str();
  state.position_x = reader.f32();
  state.position_y = reader.f32();

  // Not `!= 0`: a byte outside {0,1} means the producer and this reader
  // disagree about the record's shape, which is exactly what a boundary check
  // is for. Silently coercing it would hide the disagreement.
  const uint8_t checkpoint = reader.u8();
  if (checkpoint > 1)
    throw ParseError("start state is_checkpoint must be 0 or 1");
  state.is_checkpoint = (checkpoint == 1);

  state.play_time = reader.f32();
  state.coins = reader.u32();
  state.tuxdolls = reader.u32();
  state.bonus = decode_bonus(reader.u8(), "bonus");
  state.item_pocket = decode_bonus(reader.u8(), "item_pocket");
  state.coins_at_start = reader.u32();
  state.bonus_at_start = decode_bonus(reader.u8(), "bonus_at_start");
  state.pocket_at_start = decode_bonus(reader.u8(), "pocket_at_start");
  return state;
}

} // namespace trace

/* EOF */
