/**
 * @file    platform/http.h
 * @brief   The one HTTPS client: WinHTTP on Windows, libcurl elsewhere.
 *
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause License
 *            See LICENSE file in the project root for full license text.
 */
#pragma once

#include <filesystem>
#include <functional>
#include <string>

#include <rex/types.h>

namespace bd::platform {

struct HTTPResult {
  bool ok = false;
  int status = 0;
  std::string error;

  bool Succeeded() const { return ok && status >= 200 && status < 300; }
};

// 'total' is 0 when the server did not send a Content-Length.
using DownloadProgress = std::function<void(u64 done, u64 total)>;

// Both follow redirects.
class HTTP {
public:
  static HTTPResult Get(const std::string &url, std::string &body);
  static HTTPResult Download(const std::string &url,
                             const std::filesystem::path &dest,
                             const DownloadProgress &progress);
};

} // namespace bd::platform
