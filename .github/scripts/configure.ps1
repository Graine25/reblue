# Configures the Windows preset against the downloaded SDK. Run once before
# codegen and again after, so the generated sources.cmake is picked up.
#
# REBLUE_PCH=OFF: sccache does not cache a precompiled-header compilation, so
# the PCH would cost every run a full rebuild. It also hides a TU that stopped
# including what it uses.
#
# In: PRESET
$ErrorActionPreference = 'Stop'

$sdk = Join-Path $PWD "sdk/win-amd64"
cmake --preset $env:PRESET `
  "-DCMAKE_PREFIX_PATH=$sdk" `
  "-DREBLUE_PCH=OFF" `
  "-DCMAKE_C_COMPILER_LAUNCHER=sccache" `
  "-DCMAKE_CXX_COMPILER_LAUNCHER=sccache"
