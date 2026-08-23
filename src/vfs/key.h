/**
 * @file    vfs/key.h
 * @brief   The canonical form of a path inside a mount.
 *
 * @copyright   Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *              All rights reserved.
 * @license     BSD 3-Clause - see LICENSE
 */
#pragma once

#include <functional>
#include <string>
#include <string_view>
#include <utility>

namespace bd::vfs {

// Lowercase, backslash-separated, no drive prefix, no leading separator.
// Constructible only by normalizing, so a mount cannot be handed a raw path.
class Key {
public:
  Key() = default;

  // Guest paths arrive drive-prefixed ('game:\...') and rooted at g_bdBasePath,
  // with either separator and any casing.
  static Key FromGuestPath(std::string_view raw);

  // A path already relative to a mount root. The CSV disc root escape
  // ':d2anime\...' normalizes here too.
  static Key FromRelative(std::string_view raw);

  // Substitute the active locale into an _xx./_yy./_zz. marker. Reads guest
  // memory, so it only means anything once the guest has booted.
  Key Localized() const;

  // Both operands are canonical, so their join is too. An empty operand
  // yields the other one rather than a stray separator.
  Key operator/(const Key &child) const;

  // The half-open range [from, to) of this key. Canonical whenever both
  // bounds sit on a separator boundary or an end, which is the only way
  // callers walking separators use it.
  Key Slice(size_t from, size_t to = std::string::npos) const;

  std::string_view str() const { return key_; }
  const std::string &string() const { return key_; }
  bool empty() const { return key_.empty(); }

  bool operator==(const Key &) const = default;

private:
  explicit Key(std::string key) : key_(std::move(key)) {}

  std::string key_;
};

} // namespace bd::vfs

template <> struct std::hash<bd::vfs::Key> {
  size_t operator()(const bd::vfs::Key &k) const noexcept {
    return std::hash<std::string_view>{}(k.str());
  }
};
