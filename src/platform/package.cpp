/**
 * @file    platform/package.cpp
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause License
 *            See LICENSE file in the project root for full license text.
 */
#include "platform/package.h"

#include "core/logging.h"
#include "core/sha256.h"
#include "core/zip_unpack.h"

namespace bd::platform {

Package::Result Package::Fetch(const std::string &url,
                               const std::string &sha256,
                               const std::filesystem::path &cache_zip,
                               const std::filesystem::path &dest,
                               const DownloadProgress &progress) {
  namespace fs = std::filesystem;
  std::error_code ec;
  fs::create_directories(cache_zip.parent_path(), ec);

  if (!fs::exists(cache_zip, ec) || bd::SHA256File(cache_zip) != sha256) {
    const HTTPResult result = HTTP::Download(url, cache_zip, progress);
    if (!result.Succeeded()) {
      BD_ERROR("[package] {} download failed: {}", url, result.error);
      return Result::kDownloadFailed;
    }
    if (bd::SHA256File(cache_zip) != sha256) {
      BD_ERROR("[package] {} does not match its manifest digest", url);
      fs::remove(cache_zip, ec);
      return Result::kHashMismatch;
    }
  }

  fs::remove_all(dest, ec);
  std::string error;
  if (!bd::UnpackZip(cache_zip, dest, error)) {
    BD_ERROR("[package] unpack failed: {}", error);
    fs::remove_all(dest, ec);
    return Result::kUnpackFailed;
  }
  return Result::kOk;
}

} // namespace bd::platform
