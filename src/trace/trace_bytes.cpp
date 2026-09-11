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

#include "trace/trace_bytes.hpp"

#include <cstring>

#include "trace/trace_error.hpp"

namespace trace {

ByteWriter::ByteWriter() :
  m_data()
{
}

void
ByteWriter::u8(uint8_t v)
{
  m_data.push_back(v);
}

void
ByteWriter::u16(uint16_t v)
{
  m_data.push_back(static_cast<uint8_t>(v & 0xFFu));
  m_data.push_back(static_cast<uint8_t>((v >> 8) & 0xFFu));
}

void
ByteWriter::u32(uint32_t v)
{
  m_data.push_back(static_cast<uint8_t>(v & 0xFFu));
  m_data.push_back(static_cast<uint8_t>((v >> 8) & 0xFFu));
  m_data.push_back(static_cast<uint8_t>((v >> 16) & 0xFFu));
  m_data.push_back(static_cast<uint8_t>((v >> 24) & 0xFFu));
}

void
ByteWriter::i32(int32_t v)
{
  uint32_t bits = 0;
  std::memcpy(&bits, &v, sizeof(bits));
  u32(bits);
}

void
ByteWriter::f32(float v)
{
  static_assert(sizeof(float) == 4, "float must be 32 bits");
  uint32_t bits = 0;
  std::memcpy(&bits, &v, sizeof(bits));
  u32(bits);
}

void
ByteWriter::str(const std::string& v)
{
  if (v.find('\0') != std::string::npos)
    throw ParseError("string contains an embedded NUL");

  m_data.insert(m_data.end(), v.begin(), v.end());
  m_data.push_back(0);
}

void
ByteWriter::bytes(const uint8_t* data, size_t len)
{
  m_data.insert(m_data.end(), data, data + len);
}

ByteReader::ByteReader(const uint8_t* data, size_t size) :
  m_data(data),
  m_size(size),
  m_pos(0)
{
}

void
ByteReader::require(size_t len) const
{
  // m_pos <= m_size is a class invariant: m_pos only ever advances after a
  // successful require() call, so m_size - m_pos cannot underflow here.
  if (len > m_size - m_pos)
    throw ParseError("read past end of buffer");
}

uint8_t
ByteReader::u8()
{
  require(1);
  return m_data[m_pos++];
}

uint16_t
ByteReader::u16()
{
  require(2);
  const uint16_t lo = m_data[m_pos];
  const uint16_t hi = m_data[m_pos + 1];
  m_pos += 2;
  return static_cast<uint16_t>(lo | (hi << 8));
}

uint32_t
ByteReader::u32()
{
  require(4);
  const uint32_t b0 = m_data[m_pos];
  const uint32_t b1 = m_data[m_pos + 1];
  const uint32_t b2 = m_data[m_pos + 2];
  const uint32_t b3 = m_data[m_pos + 3];
  m_pos += 4;
  return b0 | (b1 << 8) | (b2 << 16) | (b3 << 24);
}

int32_t
ByteReader::i32()
{
  const uint32_t bits = u32();
  int32_t out = 0;
  std::memcpy(&out, &bits, sizeof(out));
  return out;
}

float
ByteReader::f32()
{
  const uint32_t bits = u32();
  float out = 0.0f;
  std::memcpy(&out, &bits, sizeof(out));
  return out;
}

std::string
ByteReader::str()
{
  size_t end = m_pos;
  while (end < m_size && m_data[end] != 0)
    ++end;

  if (end >= m_size)
    throw ParseError("unterminated string");

  std::string out(reinterpret_cast<const char*>(m_data + m_pos), end - m_pos);
  m_pos = end + 1;
  return out;
}

void
ByteReader::bytes(uint8_t* out, size_t len)
{
  require(len);
  std::memcpy(out, m_data + m_pos, len);
  m_pos += len;
}

void
ByteReader::skip(size_t len)
{
  require(len);
  m_pos += len;
}

} // namespace trace

/* EOF */
