/**
 * @file    platform/updates.h
 * @brief   Startup check for a newer re:Blue release.
 *
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause License
 *            See LICENSE file in the project root for full license text.
 */
#pragma once

#include <atomic>
#include <mutex>
#include <optional>
#include <string>

namespace bd::platform {

struct Release {
  std::string version; // the git tag, "v0.76.74"
  std::string url;     // the release page it redirected to
};

// Asks bd_update_url which release is latest and compares it with this build.
// GitHub answers /releases/latest with a 302 whose Location names the tag, so
// one request settles it and no page body is downloaded. Runs on its own
// thread: nothing on the boot path waits on it.
class Updates {
public:
  static Updates &Get();

  // One check for the life of the process, and only when bd_update_check is
  // set. Returns immediately.
  void Start();

  // Set once the check finds a release newer than this build.
  std::optional<Release> Newer() const;

private:
  Updates() = default;
  Updates(const Updates &) = delete;
  Updates &operator=(const Updates &) = delete;

  void Check(const std::string &url);

  mutable std::mutex mutex_;
  std::optional<Release> newer_;
  std::atomic<bool> started_{false};
};

} // namespace bd::platform
