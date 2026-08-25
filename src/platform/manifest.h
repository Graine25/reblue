/**
 * @file    platform/manifest.h
 * @brief   The two documents the update endpoint serves: one naming the
 *          current build, one naming the content packs on offer.
 *
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause License
 *            See LICENSE file in the project root for full license text.
 */
#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <rex/types.h>

namespace bd::platform {

// Bumped whenever a wire format changes. A document declaring anything else is
// refused whole rather than read partially. The two carry separate numbers
// because they are published on separate schedules.
inline constexpr i64 kAppManifestSchema = 1;
inline constexpr i64 kContentManifestSchema = 1;

struct Artifact {
  std::string url;
  std::string sha256;
  u64 size = 0;
};

struct ContentEntry {
  std::string id;
  i64 version = 0;
  std::string url;
  std::string sha256;
  u64 size = 0;
  std::string min_app; // empty means no minimum
};

// Compares dotted version strings on their digit runs alone, so a leading v
// falls out. Every run counts, including one inside a suffix, which orders the
// nightlies' fourth component.
class Version {
public:
  // Negative when a precedes b, 0 when equal.
  static int Compare(std::string_view a, std::string_view b);
};

// The one document a build asks for. Names the current release, and points at
// the content manifest rather than carrying it, so content publishes on its
// own schedule while every build needs exactly one baked endpoint.
class AppManifest {
public:
  static std::optional<AppManifest> Parse(std::string_view text,
                                          std::string &error);
  static std::optional<AppManifest> Fetch(const std::string &url,
                                          std::string &error);

  // "win-amd64", "linux-amd64", "linux-arm64", "mac-amd64", "mac-arm64".
  static std::string_view PlatformKey();

  const Artifact *ArtifactForThisPlatform() const;

  i64 schema = 0;
  std::string app_version;
  std::string notes_url;
  std::string content_url; // empty means this deployment serves no content
  std::unordered_map<std::string, Artifact> artifacts;
};

// Names every content pack on offer, at the url the app manifest gives. It is
// published on its own, so a pack ships without a release and reaches installs
// already out there. Packs that need a newer build say so in min_app.
class ContentManifest {
public:
  static std::optional<ContentManifest> Parse(std::string_view text,
                                              std::string &error);
  static std::optional<ContentManifest> Fetch(const std::string &url,
                                              std::string &error);

  i64 schema = 0;
  std::vector<ContentEntry> packs;
};

} // namespace bd::platform
