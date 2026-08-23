#!/usr/bin/env bash
# Writes notes.md for a dev nightly prerelease: what SDK it was built against,
# and the commits since the previous nightly.
#
# In: DEV_SHA, PREV_SHA (may be empty), SDK_REPO, SDK_TAG, DEV_BRANCH
set -euo pipefail

if [ -n "${PREV_SHA}" ] && git cat-file -e "${PREV_SHA}^{commit}" 2>/dev/null; then
  commits=$(git log --no-merges --pretty='format:- %s (%h)' "${PREV_SHA}..${DEV_SHA}")
  header="Changes since previous dev nightly (\`${PREV_SHA:0:8}\`):"
else
  commits=$(git log --no-merges --pretty='format:- %s (%h)' -20 "${DEV_SHA}")
  header="Latest commits on \`${DEV_BRANCH}\`:"
fi
[ -n "${commits}" ] || commits="- (no new commits)"

{
  printf 'Built against [rexglue-sdk `%s`](https://github.com/%s/releases/tag/%s).\n\n' \
    "${SDK_TAG}" "${SDK_REPO}" "${SDK_TAG}"
  printf '%s\n\n%s\n' "${header}" "${commits}"
} > notes.md

echo "--- notes.md ---"
cat notes.md
