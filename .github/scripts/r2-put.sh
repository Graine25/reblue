#!/usr/bin/env bash
# Uploads one file, or one directory tree, to a key in the R2 bucket.
#
#   r2-put.sh <local-path> <key> [cache-control]
#
# A local path ending in / uploads the tree under that key prefix.
#
# Pass a cache-control for anything published to a fixed key. A manifest is
# read at every launch and is the only thing that tells a client new content
# exists, so a CDN holding yesterday's copy is the same as not publishing.
# Artifacts sit at version-stamped keys and are never rewritten, so they take
# the default and cache as long as the edge likes.
#
# In: R2_ACCOUNT_ID, R2_ACCESS_KEY_ID, R2_SECRET_ACCESS_KEY, R2_BUCKET
set -euo pipefail

src="${1:?local path required}"
key="${2:?bucket key required}"
cache="${3:-}"

# GitHub-hosted runners ship the CLI already; this only fires if that changes.
if ! command -v aws >/dev/null 2>&1; then
  echo "aws CLI not found, installing"
  curl -fsSL "https://awscli.amazonaws.com/awscli-exe-linux-x86_64.zip" -o /tmp/awscliv2.zip
  unzip -q /tmp/awscliv2.zip -d /tmp
  sudo /tmp/aws/install
fi

export AWS_ACCESS_KEY_ID="${R2_ACCESS_KEY_ID}"
export AWS_SECRET_ACCESS_KEY="${R2_SECRET_ACCESS_KEY}"
export AWS_DEFAULT_REGION=auto

args=(s3 cp "${src}" "s3://${R2_BUCKET}/${key}"
      --endpoint-url "https://${R2_ACCOUNT_ID}.r2.cloudflarestorage.com")
case "${src}" in
  */) args+=(--recursive) ;;
esac
if [ -n "${cache}" ]; then
  args+=(--cache-control "${cache}")
fi

echo "Uploading ${src} to s3://${R2_BUCKET}/${key}"
aws "${args[@]}"
