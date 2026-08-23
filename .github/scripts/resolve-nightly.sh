#!/usr/bin/env bash
# Picks the dev+sdk combo for a nightly and decides whether it is new.
#
# The tag is stable per combo, so a rerun of the same combo finds its release
# already there and skips the build.
#
# In:  SDK_REPO, SDK_NIGHTLY_PREFIX, REBLUE_NIGHTLY_PREFIX, REPO, GH_TOKEN,
#      GITHUB_OUTPUT
# Out: should_build, dev_sha, dev_short, sdk_tag, sdk_short, version, tag_name,
#      asset_base, prev_nightly_sha
set -euo pipefail

dev_sha=$(git rev-parse HEAD)
dev_short="${dev_sha:0:8}"

version=$(sed -nE 's/^project\(reblue VERSION ([0-9]+\.[0-9]+\.[0-9]+).*/\1/p' CMakeLists.txt | head -n1)
if [ -z "${version}" ]; then
  echo "::error::could not parse project version from CMakeLists.txt"
  exit 1
fi

sdk_tag=$(gh release list --repo "${SDK_REPO}" --limit 100 \
  --json tagName,isPrerelease,createdAt \
  | jq -r --arg prefix "${SDK_NIGHTLY_PREFIX}" \
      '[.[] | select(.isPrerelease) | select(.tagName | startswith($prefix))]
       | sort_by(.createdAt) | last | .tagName // ""')
if [ -z "${sdk_tag}" ]; then
  echo "::error::no nightly release found under ${SDK_REPO}"
  exit 1
fi

sdk_short=$(printf '%s' "${sdk_tag}" | sed -nE 's/^nightly-[0-9]{8}-([0-9a-f]+)$/\1/p')
sdk_short="${sdk_short:0:8}"
if [ -z "${sdk_short}" ]; then
  echo "::error::could not parse a sha out of SDK tag ${sdk_tag}"
  exit 1
fi

tag_name="${REBLUE_NIGHTLY_PREFIX}${version}-d${dev_short}-s${sdk_short}"

{
  echo "dev_sha=${dev_sha}"
  echo "dev_short=${dev_short}"
  echo "sdk_tag=${sdk_tag}"
  echo "sdk_short=${sdk_short}"
  echo "version=${version}"
  echo "tag_name=${tag_name}"
  echo "asset_base=reblue-${version}-dev-${dev_short}-${sdk_short}"
} >> "${GITHUB_OUTPUT}"

echo "Candidate nightly:"
echo "  version : ${version}"
echo "  dev     : ${dev_sha}"
echo "  sdk     : ${sdk_tag}"
echo "  tag     : ${tag_name}"

# The previous nightly's commit, so the notes can diff against it.
prev_tag=$(gh release list --repo "${REPO}" --limit 100 \
  --json tagName,isPrerelease,createdAt \
  | jq -r --arg prefix "${REBLUE_NIGHTLY_PREFIX}" \
      '[.[] | select(.isPrerelease) | select(.tagName | startswith($prefix))]
       | sort_by(.createdAt) | last | .tagName // ""')
if [ -n "${prev_tag}" ]; then
  prev_nightly_sha=$(gh release view "${prev_tag}" --repo "${REPO}" \
    --json targetCommitish -q .targetCommitish 2>/dev/null || echo "")
else
  prev_nightly_sha=""
fi
echo "prev_nightly_sha=${prev_nightly_sha}" >> "${GITHUB_OUTPUT}"

if gh release view "${tag_name}" --repo "${REPO}" >/dev/null 2>&1; then
  echo "Release ${tag_name} already exists; skipping build."
  echo "should_build=false" >> "${GITHUB_OUTPUT}"
else
  echo "Release ${tag_name} does not exist; building."
  echo "should_build=true" >> "${GITHUB_OUTPUT}"
fi
