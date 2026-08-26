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

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace trace {

/** Little-endian byte sink. Every multi-byte value is written byte by byte,
    so the encoding never depends on host endianness. */
class ByteWriter final
{
public:
  ByteWriter();

  void u8(uint8_t v);
  void u16(uint16_t v);
  void u32(uint32_t v);
  void i32(int32_t v);
  void f32(float v);

  /** NUL-terminated. The string itself must not contain a NUL. */
  void str(const std::string& v);

  void bytes(const uint8_t* data, size_t len);

  inline const std::vector<uint8_t>& data() const { return m_data; }
  inline size_t size() const { return m_data.size(); }

private:
  std::vector<uint8_t> m_data;
};

/** Bounds-checked little-endian byte source. Does not own its buffer. */
class ByteReader final
{
public:
  ByteReader(const uint8_t* data, size_t size);

  uint8_t u8();
  uint16_t u16();
  uint32_t u32();
  int32_t i32();
  float f32();
  std::string str();

  void bytes(uint8_t* out, size_t len);
  void skip(size_t len);

  inline size_t remaining() const { return m_size - m_pos; }
  inline bool exhausted() const { return m_pos >= m_size; }
  inline size_t position() const { return m_pos; }

private:
  void require(size_t len) const;

  const uint8_t* m_data;
  size_t m_size;
  size_t m_pos;
};

} // namespace trace

/* EOF */
