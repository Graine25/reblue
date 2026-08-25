/**
 * @file    core/sha256.h
 * @brief   Lowercase hex SHA-256 digests over PicoSHA2.
 *
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause License
 *            See LICENSE file in the project root for full license text.
 */
#pragma once

#include <cstddef>
#include <filesystem>
#include <string>

namespace bd {

// Lowercase hex digest of a byte range.
std::string SHA256(const void *data, size_t size);

// Lowercase hex digest of a file's contents, empty if it could not be opened.
std::string SHA256File(const std::filesystem::path &path);

} // namespace bd
