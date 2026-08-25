/**
 * @file    core/zip_unpack.cpp
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause License
 *            See LICENSE file in the project root for full license text.
 */
#include "core/zip_unpack.h"

#define MINIZ_HEADER_FILE_ONLY
#include <miniz.h>

namespace bd {

bool IsUnsafeArchivePath(const std::string &path) {
  if (path.empty())
    return true;
  if (path.find('\0') != std::string::npos)
    return true;
  if (path.front() == '/' || path.front() == '\\')
    return true;
  if (path.size() >= 2 && path[1] == ':')
    return true;
  std::filesystem::path p(path);
  for (const auto &part : p) {
    if (part.string() == "..")
      return true;
  }
  return false;
}

bool UnpackZip(const std::filesystem::path &zip_path,
               const std::filesystem::path &dest, std::string &error) {
  mz_zip_archive zip{};
  if (!mz_zip_reader_init_file(&zip, zip_path.string().c_str(), 0)) {
    error = "failed to open " + zip_path.string();
    return false;
  }

  const int num_files = static_cast<int>(mz_zip_reader_get_num_files(&zip));
  std::error_code ec;
  for (int i = 0; i < num_files; ++i) {
    if (mz_zip_reader_is_file_a_directory(&zip, i))
      continue;

    // miniz truncates to the buffer and returns the bytes it wrote plus the
    // terminator, so a full buffer is exactly how a too-long name shows up.
    char fname[512];
    const mz_uint n = mz_zip_reader_get_filename(&zip, i, fname, sizeof(fname));
    if (n == 0 || n >= sizeof(fname)) {
      error = "unreadable entry name in " + zip_path.string();
      mz_zip_reader_end(&zip);
      return false;
    }
    // The archive's name is not terminated and may carry an embedded NUL, so
    // this takes the length miniz reported rather than stopping at the first.
    const std::string name(fname, n - 1);
    if (IsUnsafeArchivePath(name)) {
      error = "unsafe entry '" + name + "' in " + zip_path.string();
      mz_zip_reader_end(&zip);
      return false;
    }

    const auto out_path = dest / std::filesystem::path(name);
    std::filesystem::create_directories(out_path.parent_path(), ec);
    if (!mz_zip_reader_extract_to_file(&zip, i, out_path.string().c_str(),
                                       0)) {
      error = "failed to extract '" + name + "' from " + zip_path.string();
      mz_zip_reader_end(&zip);
      return false;
    }
  }

  mz_zip_reader_end(&zip);
  return true;
}

} // namespace bd
