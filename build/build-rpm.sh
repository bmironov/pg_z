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
    echo "  os_version   - OS version from list: $ROCKY_VERSIONS_LIST"
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

OS_FAMILY=$(get_redhat_os_family $OS_VERSION)
check_pg_version "$PG_VERSION"
OS_MIRROR=$(get_os_mirror $OS_FAMILY)
ROCKY_MAJOR=$(get_rocky_major_version $OS_VERSION)
ARCH_LEVEL=$(get_redhat_arch_level $OS_VERSION)
REPO_COMPONENTS=$(get_repo_components $OS_FAMILY $OS_VERSION)

DOCKER_TAG="postgresql${PG_VERSION}-pg-z:${PG_Z_VERSION}-1.el${ROCKY_MAJOR}"
ARCH=$(arch)
RPM_FILENAME=$(echo ${DOCKER_TAG} | tr ':' '-' | sed "s/$/.${ARCH}.rpm/")
rm -f "./${TARGET_DIR}/${RPM_FILENAME}"

echo "=== Building and Exporting Statically Linked pg_z.rpm Package ==="
echo "Target OS:  ${OS_VERSION}"
echo "PostgreSQL: v${PG_VERSION}"
echo "Extension:  pg_z v${PG_Z_VERSION}"
echo "Package:    ${RPM_FILENAME}"

DOCKER_BUILDKIT=1 docker build \
    --build-arg ROCKY_MAJOR="${ROCKY_MAJOR}" \
    --build-arg ARCH_LEVEL="${ARCH_LEVEL}" \
    --build-arg REPO_COMPONENTS="${REPO_COMPONENTS}" \
    --build-arg PG_VERSION="${PG_VERSION}" \
    --build-arg PG_Z_VERSION="${PG_Z_VERSION}" \
    --build-arg TARGET_DIR="${TARGET_DIR}" \
    --build-arg RPM_FILENAME="${RPM_FILENAME}" \
    --build-arg BROTLI_VERSION="${BROTLI_VERSION}" \
    --build-arg LZ4_VERSION="${LZ4_VERSION}" \
    --build-arg SNAPPY_VERSION="${SNAPPY_VERSION}" \
    --build-arg ZLIB_VERSION="${ZLIB_VERSION}" \
    --build-arg ZLIB_NG_VERSION="${ZLIB_NG_VERSION}" \
    --build-arg ZSTD_VERSION="${ZSTD_VERSION}" \
    --tag "${DOCKER_TAG}" \
    --target export-stage \
    --output "${TARGET_DIR}" \
    -f Dockerfile.rpm-build ..

echo "Done! RPM package successfully generated as ./${TARGET_DIR}/${RPM_FILENAME}"
