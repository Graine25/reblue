#!/usr/bin/env bash
# Configures a reblue preset against a downloaded SDK slice. Run once before
# codegen and again after, so the generated sources.cmake is picked up.
#
# REBLUE_PCH=OFF: neither sccache nor ccache caches a precompiled-header
# compilation, so the PCH would cost every run a full rebuild. It also hides a
# TU that stopped including what it uses.
#
# In: PRESET, SDK_SLICE, COMPILER_LAUNCHER (optional, e.g. ccache)
set -euo pipefail

args=(--preset "${PRESET}" "-DCMAKE_PREFIX_PATH=${PWD}/sdk/${SDK_SLICE}" -DREBLUE_PCH=OFF)
if [ -n "${COMPILER_LAUNCHER:-}" ]; then
  args+=("-DCMAKE_C_COMPILER_LAUNCHER=${COMPILER_LAUNCHER}"
         "-DCMAKE_CXX_COMPILER_LAUNCHER=${COMPILER_LAUNCHER}")
fi

cmake "${args[@]}"
