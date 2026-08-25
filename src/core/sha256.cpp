/**
 * @file    core/sha256.cpp
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause License
 *            See LICENSE file in the project root for full license text.
 */
#include "core/sha256.h"

#include <fstream>

#include <picosha2.h>

namespace bd {

std::string SHA256(const void *data, size_t size) {
  const auto *bytes = static_cast<const unsigned char *>(data);
  return picosha2::hash256_hex_string(bytes, bytes + size);
}

std::string SHA256File(const std::filesystem::path &path) {
  std::ifstream file(path, std::ios::binary);
  if (!file)
    return {};

  picosha2::byte_t hash[picosha2::k_digest_size];
  picosha2::hash256(file, hash, hash + picosha2::k_digest_size);
  return picosha2::bytes_to_hex_string(hash, hash + picosha2::k_digest_size);
}

} // namespace bd
