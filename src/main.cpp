/**
 * @file    main.cpp
 * @brief   Application entry point, registers ReblueApp with the runtime.
 *
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause License
 *            See LICENSE file in the project root for full license text.
 */
#include "generated/reblue_init.h"

#include "reblue_app.h"

REX_DEFINE_APP(reblue, ReblueApp::Create)
