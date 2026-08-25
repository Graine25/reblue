/**
 * @file    core/zip_unpack.h
 * @brief   Safe zip extraction shared by anything that unpacks an untrusted
 *          archive: a downloaded update, a mod file, a content pack.
 *
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause License
 *            See LICENSE file in the project root for full license text.
 */
#pragma once

#include <filesystem>
#include <string>

namespace bd {

// Rejects an archive entry name that would escape the directory it is
// extracted into: "..", an absolute path, or a drive-qualified path.
bool IsUnsafeArchivePath(const std::string &path);

// Extracts every regular-file entry of 'zip' under 'dest'. Fails on the first
// unsafe entry name or extraction error, writing 'error'.
bool UnpackZip(const std::filesystem::path &zip,
               const std::filesystem::path &dest, std::string &error);

} // namespace bd
