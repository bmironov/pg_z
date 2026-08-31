#!/usr/bin/env bash

set -e
cd "$(dirname "$0")"
SCRIPT_NAME=$(basename $0)
source ./build-defaults.sh
mkdir -p "$TARGET_DIR"

usage() {
    echo "Usage:"
    echo "$SCRIPT_NAME [pg_z_version]"
    echo "Where:"
    echo "pg_z_version - (optional) latest, if not provided, or tag from git repository"
}

PG_Z_VERSION=$(get_pg_z_version "$1")

DOCKER_TAG="pg-z:${PG_Z_VERSION}-archive"
ARCHIVE_NAME=$(echo ${DOCKER_TAG} | tr ':' '_')
rm -f ./${TARGET_DIR}/${ARCHIVE_NAME}*

echo "=== Building and Exporting pg_z Source Code ==="
echo "Extension:  pg_z v${PG_Z_VERSION}"
echo "Docker tag: ${DOCKER_TAG}"
echo "Archives:   ${ARCHIVE_NAME}.tar.gz / ${ARCHIVE_NAME}.zip"

# Clean and recreate the target packages directory on your local PC host
rm -rf ${TARGET_DIR}/*.zip ${TARGET_DIR}/*.tar.gz

DOCKER_BUILDKIT=1 docker build \
    --build-arg PG_Z_VERSION="${PG_Z_VERSION}" \
    --build-arg TARGET_DIR="${TARGET_DIR}" \
    --build-arg ARCHIVE_NAME="${ARCHIVE_NAME}" \
    --tag "${DOCKER_TAG}" \
    --target export-stage \
    --output ${TARGET_DIR} \
    -f Dockerfile.zip-build ..

echo "Done! Packages successfully generated inside ./${TARGET_DIR} directory."
