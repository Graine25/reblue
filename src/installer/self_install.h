/**
 * @file    installer/self_install.h
 * @brief   Copies the running program into the chosen install directory and
 *          relaunches from there. Windows only: the DLL-next-to-the-exe
 *          loading problem this solves does not exist on the other
 *          platforms, whose packages already anchor at a writable location.
 *
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause License
 *            See LICENSE file in the project root for full license text.
 */
#pragma once

#include <filesystem>
#include <string>

namespace bd::installer {

#if defined(_WIN32)

bool CopyProgramTo(const std::filesystem::path &install, std::string &error);

#endif // defined(_WIN32)

} // namespace bd::installer
