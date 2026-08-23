#!/usr/bin/env bash
# The vendored dxc-bin ships no aarch64 Linux DirectXShaderCompiler and neither
# Microsoft nor Debian publishes one, so take the pair dxc-bin keeps on a branch
# it never merged. Pinned by commit, checked by digest, dropped where dxc-bin's
# own CMakeLists already looks.
set -euo pipefail

DXC_BIN_COMMIT=781065589d5dba23598b746b3d2e457e985b1442
DXC_BIN_DIR=thirdparty/XenosRecomp/thirdparty/dxc-bin

fetch() {  # <path under dxc-bin> <sha256> <mode>
  local out="${DXC_BIN_DIR}/$1"
  mkdir -p "$(dirname "${out}")"
  curl -fsSL -o "${out}" \
    "https://raw.githubusercontent.com/renderbag/dxc-bin/${DXC_BIN_COMMIT}/$1"
  echo "$2  ${out}" | sha256sum -c -
  chmod "$3" "${out}"
}

fetch lib/arm64/libdxcompiler.so \
  e1f5a8debc11eac62bfe7f64417dba13eae21c014387787e6bdabf5324942364 644
fetch bin/arm64/dxc-linux \
  07fefd4c02afeecf37f614b6670ed2cbfae520aba9520c94bb04e4ed135e7863 755

LD_LIBRARY_PATH="$(pwd)/${DXC_BIN_DIR}/lib/arm64" \
  "${DXC_BIN_DIR}/bin/arm64/dxc-linux" --version
