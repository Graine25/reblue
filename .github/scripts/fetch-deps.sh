#!/usr/bin/env bash
# Downloads one platform slice of a rexglue-sdk release into ./sdk/$SDK_SLICE.
# Uses the plain API rather than gh, which the ubuntu:22.04 container lacks.
#
# In: SDK_REPO, SDK_TAG, SDK_SLICE (win-amd64 | linux-amd64 | linux-arm64 |
#     mac-arm64 | mac-amd64), GH_TOKEN, ASSETS_REPO, ASSETS_TOKEN
set -euo pipefail

url=$(curl -fsSL -H "Authorization: Bearer ${GH_TOKEN}" \
  "https://api.github.com/repos/${SDK_REPO}/releases/tags/${SDK_TAG}" \
  | jq -r --arg slice "${SDK_SLICE}" \
      '.assets[] | select(.name | endswith($slice + ".zip")) | .browser_download_url' \
  | head -n1)

if [ -z "${url}" ]; then
  echo "::error::${SDK_REPO} ${SDK_TAG} publishes no ${SDK_SLICE} asset"
  exit 1
fi

curl -fsSL -o sdk.zip "${url}"
unzip -q sdk.zip -d sdk
if [ ! -d "sdk/${SDK_SLICE}" ]; then
  echo "::error::SDK archive has no ${SDK_SLICE}/ directory"
  exit 1
fi
echo "Extracted $(basename "${url}") to sdk/${SDK_SLICE}"

git clone --depth 1 \
  "https://x-access-token:${ASSETS_TOKEN}@github.com/${ASSETS_REPO}.git" assets
git -C assets remote set-url origin "https://github.com/${ASSETS_REPO}.git"
echo "Cloned ${ASSETS_REPO} to assets"
