#!/usr/bin/env bash
# Publishes a release's artifacts and app manifest to the R2 bucket the update
# endpoint fronts. The manifest goes last: it is what a running build reads
# first, so it must never name artifacts that are not up yet.
#
# Content packs are published separately, not by CI.
#
# In: VERSION, PRERELEASE ('true' | 'false'), and the R2_* variables r2-put.sh
#     reads.
set -euo pipefail

case "${PRERELEASE}" in
  true)  channel_file=nightly.toml ;;
  false) channel_file=stable.toml ;;
  *)
    echo "::error::PRERELEASE must be 'true' or 'false', got '${PRERELEASE}'"
    exit 1
    ;;
esac

# A manifest is read at every launch and is the only thing that tells a client
# new content exists, so the edge must not hold yesterday's copy.
nocache="no-cache, max-age=0, must-revalidate"

here="$(dirname "$0")"
"${here}/r2-put.sh" artifacts/ "app/v${VERSION}/"
"${here}/r2-put.sh" artifacts/manifest.toml "manifest/${channel_file}" "${nocache}"

echo "Published v${VERSION} to app/v${VERSION}/ and manifest/${channel_file}"
