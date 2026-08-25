#!/usr/bin/env bash
# Publishes a release's artifacts and app manifest to the R2 bucket the update
# endpoint fronts.
#
# Two prefixes, and only one of them is a door. app/v<version>/ holds the
# builds, at keys that are written once and never rewritten. manifest/ holds
# the one file a client asks for by name, which is the channel it was built
# against. The manifest goes up last: it is read first, so it must never name
# artifacts that are not up yet.
#
# The manifest is written beside artifacts/ rather than inside it. The whole
# directory goes to the versioned prefix, so a manifest in there would ship a
# second copy at a key nothing reads.
#
# Content packs are published separately, not by CI.
#
# In: VERSION, PRERELEASE ('true' | 'false'), MANIFEST (default manifest.toml),
#     and the R2_* variables r2-put.sh reads.
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

manifest="${MANIFEST:-manifest.toml}"
if [ ! -f "${manifest}" ]; then
  echo "::error::no manifest at ${manifest}"
  exit 1
fi

here="$(dirname "$0")"
"${here}/r2-put.sh" artifacts/ "app/v${VERSION}/"
"${here}/r2-put.sh" "${manifest}" "manifest/${channel_file}" "${nocache}"

echo "Published v${VERSION} to app/v${VERSION}/ and manifest/${channel_file}"
