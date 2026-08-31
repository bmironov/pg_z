#!/usr/bin/env bash

# ==============================================================================
# GLOBAL CONSTANTS
# ==============================================================================
export ERR_NO=0
export ERR_NOT_ENOUGH_PARAMETERS=1
export ERR_UNKNOWN_OS=2
export ERR_UNKNOWN_PG=3

export TARGET_DIR="packages"

export OS_FAMILY_DEBIAN="debian"
export OS_FAMILY_UBUNTU="ubuntu"
SUPPORTED_OS_LIST="${OS_FAMILY_DEBIAN}, ${OS_FAMILY_UBUNTU}"

SUPPORTED_DEBIAN_VERSIONS=("bookworm" "trixie")
SUPPORTED_UBUNTU_VERSIONS=("noble" "jammy" "resolute")
IFS='|' DEBIAN_VERSIONS_REGEX="^(${SUPPORTED_DEBIAN_VERSIONS[*]})$"
IFS=', ' DEBIAN_VERSIONS_LIST="${SUPPORTED_DEBIAN_VERSIONS[*]}"
IFS='|' UBUNTU_VERSIONS_REGEX="^(${SUPPORTED_UBUNTU_VERSIONS[*]})$"
IFS=', ' UBUNTU_VERSIONS_LIST="${SUPPORTED_UBUNTU_VERSIONS[*]}"
IFS=', ' OS_VERSIONS_LIST="${SUPPORTED_DEBIAN_VERSIONS[*]},${SUPPORTED_UBUNTU_VERSIONS[*]}"

SUPPORTED_PG_VERSIONS=("16" "17" "18")
IFS='|' PG_VERSIONS_REGEX="^(${SUPPORTED_PG_VERSIONS[*]})$"
IFS=', ' PG_VERSIONS_LIST="${SUPPORTED_PG_VERSIONS[*]}"

# Compression libraries versions
export BROTLI_VERSION="1.2.0"
export LZ4_VERSION="1.10.0"
export SNAPPY_VERSION="1.2.2"
export ZLIB_VERSION="1.3.2"
export ZLIB_NG_VERSION="2.3.3"
export ZSTD_VERSION="1.5.7"

get_pg_z_version() {
    local PG_Z_VERSION=$1

    [ "a$PG_Z_VERSION" == "a" ] && PG_Z_VERSION=$(git describe --tags --abbrev=0 2>/dev/null || echo "")
    [ "a$PG_Z_VERSION" == "a" ] && PG_Z_VERSION="0.0.1"
    echo "$PG_Z_VERSION"
}

get_os_family() {
    local OS_VERSION=$1

    if [[ "$OS_VERSION" =~ $DEBIAN_VERSIONS_REGEX ]]; then
        echo "$OS_FAMILY_DEBIAN"
    elif [[ "$OS_VERSION" =~ $UBUNTU_VERSIONS_REGEX ]]; then
        echo "$OS_FAMILY_UBUNTU"
    else
        echo "Requested OS version '$OS_VERSION' is not in supported list ($OS_VERSIONS_LIST)" >&2
        exit $ERR_UNKNOWN_OS
    fi
}

get_os_mirror() {
    local OS_FAMILY=$1

    case "$OS_FAMILY" in
    $OS_FAMILY_DEBIAN)
        echo "http://deb.debian.org"
        # echo "http://cdn-aws.deb.debian.org"
        ;;
    $OS_FAMILY_UBUNTU)
        echo "http://archive.ubuntu.com"
        # echo "http://cdn-aws.archive.ubuntu.com"
        ;;
    *)
        echo "Requested OS '$OS_FAMILY' is not in supported list ($SUPPORTED_OS_LIST)" >&2
        exit $ERR_UNKNOWN_OS
        ;;
    esac
}

check_pg_version() {
    local PG_VERSION=$1

    echo -n "Request for PostgreSQL v${PG_VERSION}: "

    if [[ "$PG_VERSION" =~ $PG_VERSIONS_REGEX ]]; then
        echo "supported"
    else
        echo "unsupported. Pick one from: $PG_VERSIONS_LIST"
        exit $ERR_UNKNOWN_PG
    fi
}

get_repo_components() {
    local OS_FAMILY=$1

    case "$OS_FAMILY" in
    $OS_FAMILY_DEBIAN)
        echo "main"
        ;;
    $OS_FAMILY_UBUNTU)
        echo "main universe"
        ;;
    *)
        echo "Requested OS '$OS_FAMILY' is not in supported list ($SUPPORTED_OS_LIST)" >&2
        exit $ERR_UNKNOWN_OS
        ;;
    esac
}

get_security_path() {
    local OS_FAMILY=$1

    case "$OS_FAMILY" in
    $OS_FAMILY_DEBIAN)
        echo "debian-security"
        ;;
    $OS_FAMILY_UBUNTU)
        echo "ubuntu"
        ;;
    *)
        echo "Requested OS '$OS_FAMILY' is not in supported list ($SUPPORTED_OS_LIST)" >&2
        exit $ERR_UNKNOWN_OS
        ;;
    esac
}
