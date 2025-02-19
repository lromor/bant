// bant - Bazel Navigation Tool
// Copyright (C) 2024 Henner Zeller <h.zeller@acm.org>
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 2 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License along
// with this program; if not, write to the Free Software Foundation, Inc.,
// 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA

#ifndef BANT_TOOL_HEADER_PROVIDER_
#define BANT_TOOL_HEADER_PROVIDER_

#include <ostream>
#include <string>

#include "absl/container/btree_map.h"
#include "bant/frontend/project-parser.h"
#include "bant/session.h"

namespace bant {

// Map describing what file has been provided by which target.
using ProvidedFromTargetMap = absl::btree_map<std::string, BazelTarget>;

// Givent the "project", creates a mapping of all headers that are exported by
// cc_library() targets to their respective targets.
ProvidedFromTargetMap ExtractHeaderToLibMapping(const ParsedProject &project,
                                                std::ostream &info_out);

// Find all the output generated by genrules.
ProvidedFromTargetMap ExtractGeneratedFromGenrule(const ParsedProject &project,
                                                  std::ostream &info_out);

// Pretty provided files and targets they are coming from in two columns.
void PrintProvidedSources(Session &session, const std::string &table_header,
                          const BazelPattern &pattern,
                          const ProvidedFromTargetMap &header_to_lib);
}  // namespace bant

#endif  // BANT_TOOL_HEADER_PROVIDER_
