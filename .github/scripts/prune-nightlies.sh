#!/usr/bin/env bash
# Keeps the $REBLUE_NIGHTLY_RETENTION most recent nightly prereleases and
# deletes the rest, tags included.
#
# In: REPO, REBLUE_NIGHTLY_PREFIX, REBLUE_NIGHTLY_RETENTION, GH_TOKEN
set -euo pipefail

gh release list --repo "${REPO}" --limit 200 \
  --json tagName,isPrerelease,createdAt \
  | jq -r --arg prefix "${REBLUE_NIGHTLY_PREFIX}" \
          --argjson keep "${REBLUE_NIGHTLY_RETENTION}" \
      '[.[] | select(.isPrerelease) | select(.tagName | startswith($prefix))]
       | sort_by(.createdAt) | reverse | .[$keep:] | .[].tagName' \
  | while IFS= read -r tag; do
      if [ -z "${tag}" ]; then continue; fi
      echo "Pruning ${tag}"
      gh release delete "${tag}" --repo "${REPO}" --yes --cleanup-tag
    done
