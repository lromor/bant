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

#include "bant/frontend/project-parser.h"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "absl/strings/str_cat.h"
#include "bant/frontend/parser.h"
#include "bant/types-bazel.h"
#include "bant/util/file-utils.h"
#include "bant/util/query-utils.h"

namespace bant {
namespace {

// Given a BUILD, BUILD.bazel filename, return the bare project path with
// no prefix or suffix.
// ./foo/bar/baz/BUILD.bazel turns into foo/bar/baz
std::string_view TargetPathFromBuildFile(std::string_view file) {
  file = file.substr(0, file.find_last_of('/'));  // Remove BUILD-file
  while (!file.empty() && (file[0] == '.' || file[0] == '/')) {
    file.remove_prefix(1);
  }
  return file;
}

// First foo/bar/baz/BUILD -> @foo//bar/baz
std::optional<BazelPackage> PackageFromExternal(std::string_view path) {
  path = TargetPathFromBuildFile(path);  // remove BUILD
  return BazelPackage::ParseFrom(absl::StrCat("@", path));
}

// Assemble a path that points to the symbolik link bazel generates for
// the external location.
// TODO: get from workspace
static FilesystemPath ExternalProjectDir() {
  const std::string project_dir_name =
    std::filesystem::current_path().filename().string();
  const std::string external_base = absl::StrCat("./bazel-", project_dir_name);
  return FilesystemPath(absl::StrCat(external_base, "/external"));
}

// Convenience function to just collect all the BUILD files. Update "stats"
// with total files searched and total time.
// If pattern contains a project name, the path is resolved from "workspace".
std::vector<FilesystemPath> CollectBuildFiles(Session &session,
                                              const BazelWorkspace &workspace,
                                              const BazelPattern &pattern) {
  bant::Stat &stats =
    session.GetStatsFor("BUILD file glob walk", "files/directories");

  bool recursive = pattern.is_recursive();
  std::string start_dir;
  if (!pattern.project().empty()) {
    auto dir_or = workspace.FindPathByProject(pattern.project());
    if (!dir_or.has_value()) {
      std::cerr << "Unknown project " << pattern.project() << "\n";
      return {};
    }
    start_dir = dir_or->path();
    start_dir.append("/");
  }
  start_dir.append(pattern.path());
  if (start_dir.empty()) start_dir = ".";

  std::vector<FilesystemPath> build_files;
  ScopedTimer timer(&stats.duration);
  const auto relevant_build_file_predicate = [](const FilesystemPath &file) {
    const std::string_view basename = file.filename();
    return basename == "BUILD" || basename == "BUILD.bazel";
  };

  const auto dir_predicate = [&](const FilesystemPath &dir) {
    if (!recursive) return false;  // Only looking at one level.
    if (dir.is_symlink()) return false;
    const std::string_view filename = dir.filename();
    // Skip irrelevant stuff
    if (filename == "_tmp") return false;
    if (filename == ".cache") return false;
    if (filename == ".git") return false;
    return true;
  };

  // File in the general project
  stats.count =
    CollectFilesRecursive(FilesystemPath(start_dir), build_files, dir_predicate,
                          relevant_build_file_predicate);

  return build_files;
}
}  // namespace

ParsedProject::ParsedProject(bool verbose)
    : external_prefix_(absl::StrCat(ExternalProjectDir().path(), "/")) {
  arena_.SetVerbose(verbose);
}

void ParsedProject::FillFromPattern(Session &session,
                                    const BazelWorkspace &workspace,
                                    const BazelPattern &pattern) {
  const auto build_files = CollectBuildFiles(session, workspace, pattern);
  for (const FilesystemPath &build_file : build_files) {
    AddBuildFile(session, build_file);
  }
}

const ParsedBuildFile *ParsedProject::AddBuildFile(
  Session &session, const FilesystemPath &build_file) {
  const std::string &filename = build_file.path();
  BazelPackage package;
  if (filename.starts_with(external_prefix_)) {
    std::string_view project_extract(filename);
    project_extract.remove_prefix(external_prefix_.size());
    auto opt_package = PackageFromExternal(project_extract);
    if (!opt_package.has_value()) {
      std::cerr << filename << ": Can't parse as package\n";
      return nullptr;
    }
    package = *opt_package;
  } else {
    package.path = TargetPathFromBuildFile(filename);
  }
  return AddBuildFile(session, build_file, package);
}

const ParsedBuildFile *ParsedProject::AddBuildFile(
  Session &session,
  const FilesystemPath &build_file,  //
  const BazelPackage &package) {
  Stat &parse_stat = session.GetStatsFor("Parsed", "BUILD files");
  ScopedTimer timer(&parse_stat.duration);
  std::optional<std::string> content = ReadFileToString(build_file);
  if (!content.has_value()) {
    std::cerr << "Could not read " << build_file.path() << "\n";
    ++error_count_;
    return nullptr;
  }

  const std::string &filename = build_file.path();
  auto inserted = file_to_parsed_.emplace(
    package, new ParsedBuildFile(filename, std::move(*content)));
  if (!inserted.second) {
    session.info() << filename << ": Already seen\n";
    return inserted.first->second.get();
  }

  ParsedBuildFile &parse_result = *inserted.first->second;
  ++parse_stat.count;
  const size_t bytes_processed = parse_result.source.size();
  if (parse_stat.bytes_processed.has_value()) {
    *parse_stat.bytes_processed += bytes_processed;
  } else {
    parse_stat.bytes_processed = bytes_processed;
  }

  parse_result.package = package;

  Scanner scanner(parse_result.source);
  std::stringstream error_collect;
  Parser parser(&scanner, &arena_, error_collect);
  parse_result.ast = parser.parse();
  parse_result.errors = error_collect.str();
  if (parser.parse_error()) {
    session.error() << error_collect.str();
    ++error_count_;
  }

  return inserted.first->second.get();
}

const ParsedBuildFile *ParsedProject::FindParsedOrNull(
  const BazelPackage &package) const {
  auto found = file_to_parsed_.find(package);
  if (found == file_to_parsed_.end()) return nullptr;
  return found->second.get();
}

void PrintProject(const BazelPattern &pattern, std::ostream &out,
                  std::ostream &info_out, const ParsedProject &project,
                  bool only_files_with_errors) {
  for (const auto &[package, file_content] : project.ParsedFiles()) {
    if (only_files_with_errors && file_content->errors.empty()) {
      continue;
    }
    if (!pattern.Match(package)) {
      continue;
    }

    out << "# " << file_content->source.name() << "\n";
    if (pattern.is_recursive()) {
      info_out << file_content->errors;
      out << file_content->package.ToString() << " = ";
      PrintVisitor(out).WalkNonNull(file_content->ast);
      out << "\n";
    } else {
      query::FindTargets(
        file_content->ast, {}, [&](const query::Result &result) {
          auto self = BazelTarget::ParseFrom(result.name, package);
          if (!self.has_value() || !pattern.Match(*self)) {
            return;
          }
          out << *self << " = ";
          PrintVisitor(out).WalkNonNull(result.node);
          out << "\n";
        });
    }
  }
}
}  // namespace bant
