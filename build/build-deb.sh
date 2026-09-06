#!/usr/bin/env bash

set -e
cd "$(dirname "$0")"
SCRIPT_NAME=$(basename $0)
source ./build-common.sh
mkdir -p "$TARGET_DIR"

usage() {
    echo "Usage:"
    echo "  $SCRIPT_NAME os_version pg_version [pg_z_version]"
    echo "Where:"
    echo "  os_version   - OS version from list: $DEBIAN_VERSIONS_LIST"
    echo "  pg_version   - PostgreSQL version from list: $PG_VERSIONS_LIST"
    echo "  pg_z_version - (optional) latest, if not provided, or tag from git repository"
}

OS_VERSION=$1
PG_VERSION=$2
PG_Z_VERSION=$(get_pg_z_version "$3")

# Parameters validation
if [ "a$OS_VERSION" == "a" -o "a$PG_VERSION" == "a" ]; then
    echo "Not enough parameters!"
    echo
    usage
    exit $ERR_NOT_ENOUGH_PARAMETERS
fi

OS_FAMILY=$(get_debian_os_family $OS_VERSION)
check_pg_version $PG_VERSION
OS_MIRROR=$(get_os_mirror $OS_FAMILY)
REPO_COMPONENTS=$(get_repo_components $OS_FAMILY)
SECURITY_PATH=$(get_security_path $OS_FAMILY)

DOCKER_TAG="postgresql-${PG_VERSION}-pg-z:${PG_Z_VERSION}-1-${OS_VERSION}"
DEB_FILENAME=$(echo ${DOCKER_TAG} | tr ':' '_' | sed 's/$/_amd64.deb/')
rm -f ./${TARGET_DIR}/${DEB_FILENAME}

echo "=== Building and Exporting Statically Linked pg_z.deb Package ==="
echo "Target OS:  ${OS_FAMILY}-${OS_VERSION}"
echo "PostgreSQL: v${PG_VERSION}"
echo "Extension:  pg_z v${PG_Z_VERSION}"
echo "Docker tag: ${DOCKER_TAG}"
echo "Package:    ${DEB_FILENAME}"

DOCKER_BUILDKIT=1 docker build \
    --build-arg OS_FAMILY="${OS_FAMILY}" \
    --build-arg OS_VERSION="${OS_VERSION}" \
    --build-arg OS_MIRROR="${OS_MIRROR}" \
    --build-arg REPO_COMPONENTS="${REPO_COMPONENTS}" \
    --build-arg SECURITY_PATH="${SECURITY_PATH}" \
    --build-arg PG_VERSION="${PG_VERSION}" \
    --build-arg PG_Z_VERSION="${PG_Z_VERSION}" \
    --build-arg TARGET_DIR="${TARGET_DIR}" \
    --build-arg DEB_FILENAME="${DEB_FILENAME}" \
    --build-arg BROTLI_VERSION="${BROTLI_VERSION}" \
    --build-arg LZ4_VERSION="${LZ4_VERSION}" \
    --build-arg SNAPPY_VERSION="${SNAPPY_VERSION}" \
    --build-arg ZLIB_VERSION="${ZLIB_VERSION}" \
    --build-arg ZLIB_NG_VERSION="${ZLIB_NG_VERSION}" \
    --build-arg ZSTD_VERSION="${ZSTD_VERSION}" \
    --tag "${DOCKER_TAG}" \
    --target export-stage \
    --output "${TARGET_DIR}" \
    -f Dockerfile.deb-build ..

echo "Done! Debian packages successfully generated as ./${TARGET_DIR}/${DEB_FILENAME}"
