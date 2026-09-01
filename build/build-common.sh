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
export OS_FAMILY_ROCKY="rocky"
SUPPORTED_OS_LIST="${OS_FAMILY_DEBIAN}, ${OS_FAMILY_UBUNTU}, ${OS_FAMILY_ROCKY}"

SUPPORTED_DEBIAN_VERSIONS=("bookworm" "trixie")
IFS='|' DEBIAN_VERSIONS_REGEX="^(${SUPPORTED_DEBIAN_VERSIONS[*]})$"
IFS=', ' DEBIAN_VERSIONS_LIST="${SUPPORTED_DEBIAN_VERSIONS[*]}"
SUPPORTED_UBUNTU_VERSIONS=("noble" "jammy" "resolute")
IFS='|' UBUNTU_VERSIONS_REGEX="^(${SUPPORTED_UBUNTU_VERSIONS[*]})$"
IFS=', ' UBUNTU_VERSIONS_LIST="${SUPPORTED_UBUNTU_VERSIONS[*]}"
SUPPORTED_ROCKY_VERSIONS=("rocky8" "rocky9" "rocky10")
IFS='|' ROCKY_VERSIONS_REGEX="^(${SUPPORTED_ROCKY_VERSIONS[*]})$"
IFS=', ' ROCKY_VERSIONS_LIST="${SUPPORTED_ROCKY_VERSIONS[*]}"

IFS=', ' DEBIAN_VERSIONS_LIST="${SUPPORTED_DEBIAN_VERSIONS[*]},${SUPPORTED_UBUNTU_VERSIONS[*]}"
IFS=', ' REDHAT_VERSIONS_LIST="${SUPPORTED_ROCKY_VERSIONS[*]}"

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

get_debian_os_family() {
    local OS_VERSION=$1

    if [[ "$OS_VERSION" =~ $DEBIAN_VERSIONS_REGEX ]]; then
        echo "$OS_FAMILY_DEBIAN"
    elif [[ "$OS_VERSION" =~ $UBUNTU_VERSIONS_REGEX ]]; then
        echo "$OS_FAMILY_UBUNTU"
    else
        echo "Requested OS version '$OS_VERSION' is not in supported list ($DEBIAN_VERSIONS_LIST)" >&2
        exit $ERR_UNKNOWN_OS
    fi
}

get_redhat_os_family() {
    local OS_VERSION=$1

    if [[ "$OS_VERSION" =~ $ROCKY_VERSIONS_REGEX ]]; then
        echo "$OS_FAMILY_ROCKY"
    else
        echo "Requested OS version '$OS_VERSION' is not in supported list ($REDHAT_VERSIONS_LIST)" >&2
        exit $ERR_UNKNOWN_OS
    fi
}

get_os_mirror() {
    local OS_FAMILY=$1

    OS_MIRROR=""
    case "$OS_FAMILY" in
    $OS_FAMILY_DEBIAN)
        OS_MIRROR="http://cdn-aws.deb.debian.org"
        [ "$GIHUB_ACTIONS" = "true" ] && OS_MIRROR="http://deb.debian.org"
        ;;
    $OS_FAMILY_UBUNTU)
        OS_MIRROR="http://cdn-aws.archive.ubuntu.com"
        [ "$GIHUB_ACTIONS" = "true" ] && OS_MIRROR="http://archive.ubuntu.com"
        ;;
    $OS_FAMILY_ROCKY)
        OS_MIRROR="http://abc.org"
        [ "$GIHUB_ACTIONS" = "true" ] && OS_MIRROR="http://xyz.org"
        ;;
    *)
        echo "Requested OS '$OS_FAMILY' is not in supported list ($SUPPORTED_OS_LIST)" >&2
        exit $ERR_UNKNOWN_OS
        ;;
    esac

    echo "${OS_MIRROR}"
}

get_rocky_major_version() {
    local OS_VERSION=$1

    ROCKY_MAJOR=$(echo "$OS_VERSION" | grep -o -E '[0-9]+')
    if [ -z "$ROCKY_MAJOR" ]; then
        echo "Requested OS version '$OS_VERSION' is not in supported list ($REDHAT_VERSIONS_LIST)" >&2
        exit $ERR_UNKNOWN_OS
    fi

    echo $ROCKY_MAJOR
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
    local OS_VERSION=$2

    case "$OS_FAMILY" in
    $OS_FAMILY_DEBIAN) echo "main" ;;
    $OS_FAMILY_UBUNTU) echo "main universe" ;;
    $OS_FAMILY_ROCKY)
        if [ -z "$OS_VERSION" ]; then
            echo "Please provide OS version as 2nd parameter" >&2
            exit $ERR_UNKNOWN_OS
        fi
        local ROCKY_MAJOR=$(get_rocky_major_version $OS_VERSION)
        case "$ROCKY_MAJOR" in
        "8") echo "powertools" ;;
        "9" | "10") echo "crb" ;;
        *)
            echo "Provided unsupported version $OS_VERSION" >&2
            exit $ERR_UNKNOWN_OS
            ;;
        esac
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
