/**
 * @file    installer/installer.h
 * @brief   The install registry is always available. The extractor and its
 *          wizard exist only in a REBLUE_BUILD_INSTALLER build.
 * @copyright   Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *              All rights reserved.
 * @license     BSD 3-Clause - see LICENSE
 */
#pragma once

#include "installer/install_registry.h"

#ifdef REBLUE_BUILD_INSTALLER
#include "installer/disc_install.h"
#include "installer/installer_wizard.h"
#endif
