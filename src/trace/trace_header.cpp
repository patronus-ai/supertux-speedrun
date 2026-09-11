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

#include "trace/trace_header.hpp"

#include "trace/trace_bytes.hpp"
#include "trace/trace_error.hpp"

namespace trace {

std::vector<uint8_t>
Header::encode() const
{
  ByteWriter writer;
  writer.str(engine_version);
  writer.str(platform);
  writer.str(level_path);
  writer.bytes(level_md5.data(), level_md5.size());
  writer.i32(seed);
  writer.f32(game_time_origin);
  writer.f32(step_rate);
  writer.u32(step_count);
  writer.f32(play_time);
  writer.u8(static_cast<uint8_t>(producer_kind));
  writer.str(provenance);
  writer.u8(static_cast<uint8_t>(outcome));
  writer.u32(deaths);
  writer.u32(coins);
  writer.u32(viewport_width);
  writer.u32(viewport_height);
  writer.u8(static_cast<uint8_t>(screen_shake_mode));
  writer.f32(camera_peek_multiplier);
  writer.u8(static_cast<uint8_t>(start_kind));
  return writer.data();
}

Header
Header::decode(ByteReader& reader)
{
  Header header;
  header.engine_version = reader.str();
  header.platform = reader.str();
  header.level_path = reader.str();
  reader.bytes(header.level_md5.data(), header.level_md5.size());
  header.seed = reader.i32();
  header.game_time_origin = reader.f32();
  header.step_rate = reader.f32();
  header.step_count = reader.u32();
  header.play_time = reader.f32();

  const uint8_t producer = reader.u8();
  if (producer > static_cast<uint8_t>(ProducerKind::AGENT))
    throw ParseError("unknown producer kind");
  header.producer_kind = static_cast<ProducerKind>(producer);

  header.provenance = reader.str();

  const uint8_t outcome = reader.u8();
  if (outcome > static_cast<uint8_t>(Outcome::ABORTED))
    throw ParseError("unknown outcome");
  header.outcome = static_cast<Outcome>(outcome);

  header.deaths = reader.u32();
  header.coins = reader.u32();

  header.viewport_width = reader.u32();
  header.viewport_height = reader.u32();

  const uint8_t shake = reader.u8();
  if (shake > static_cast<uint8_t>(ScreenShakeMode::FULL))
    throw ParseError("unknown screen shake mode");
  header.screen_shake_mode = static_cast<ScreenShakeMode>(shake);

  header.camera_peek_multiplier = reader.f32();

  const uint8_t start = reader.u8();
  if (start > static_cast<uint8_t>(StartKind::UNKNOWN))
    throw ParseError("unknown start kind");
  header.start_kind = static_cast<StartKind>(start);

  return header;
}

} // namespace trace

/* EOF */
