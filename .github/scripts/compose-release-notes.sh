#!/usr/bin/env bash
# In: REPO, TAG, GH_TOKEN
set -euo pipefail

gh api --method POST "repos/${REPO}/releases/generate-notes" \
  -f tag_name="${TAG}" \
  -q .body > notes.md

echo "--- notes.md ---"
cat notes.md
