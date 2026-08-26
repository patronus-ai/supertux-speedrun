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

#include <stdexcept>
#include <string>

namespace trace {

/** Thrown on malformed input by every decode path, and on invalid data by
    the few encode paths that can reject their argument. Decoding must never
    produce a partially-populated object; it either succeeds or throws. */
class ParseError final : public std::runtime_error
{
public:
  explicit ParseError(const std::string& what) :
    std::runtime_error("trace parse error: " + what)
  {}
};

} // namespace trace

/* EOF */
