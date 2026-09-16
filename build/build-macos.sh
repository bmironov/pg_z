#!/usr/bin/env bash

set -e
cd "$(dirname "$0")"
SCRIPT_NAME=$(basename $0)
source ./build-common.sh
mkdir -p "$TARGET_DIR"

PG_VERSION=$1
PG_Z_VERSION=$(get_pg_z_version "$2")

export TOP=$(pwd)
export PATH="/opt/homebrew/opt/postgresql@${PG_VERSION}/bin:$PATH"
export OPT=${TOP}/"opt"
export SRC=${TOP}/"src"

usage() {
    echo "Usage:"
    echo "  $SCRIPT_NAME pg_version [pg_z_version]"
    echo "Where:"
    echo "  pg_version   - PostgreSQL version from list: $PG_VERSIONS_LIST"
    echo "  pg_z_version - (optional) latest, if not provided, or tag from git repository"
}

workdir() {
    mkdir -p $1
    cd $1
}

# Parameters validation
if [ "a$PG_VERSION" == "a" ]; then
    echo "Not enough parameters!"
    echo
    usage
    exit $ERR_NOT_ENOUGH_PARAMETERS
fi

mkdir -p "${OPT}/include" "${OPT}/lib64" "${SRC}"

# --- LZ4 ---
workdir ${SRC}/lz4
curl -L -O "https://github.com/lz4/lz4/archive/refs/tags/v${LZ4_VERSION}.tar.gz"
tar -xzf v${LZ4_VERSION}.tar.gz
cd lz4-${LZ4_VERSION}
make -C lib clean
make -C lib install \
    PREFIX=${OPT} \
    INCLUDEDIR=${OPT}/include \
    LIBDIR=${OPT}/lib64 \
    CC="gcc -fPIC -g -O3"

# --- Zstd ---
workdir ${SRC}/zstd
curl -L -O "https://github.com/facebook/zstd/archive/refs/tags/v${ZSTD_VERSION}.tar.gz"
tar -xzf v${ZSTD_VERSION}.tar.gz
cd zstd-${ZSTD_VERSION}/build/cmake
mkdir -p build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
    -DZSTD_BUILD_SHARED=OFF \
    -DZSTD_BUILD_STATIC=ON \
    -DCMAKE_INSTALL_PREFIX="${OPT}" \
    -DCMAKE_INSTALL_LIBDIR=lib64
cmake --build . --parallel $(sysctl -n hw.ncpu)
cmake --install .

# =============== pg_z build ================
autoreconf -ifv
export CFLAGS="-I${OPT}/include -pthread -Wno-vla"
export LDFLAGS="-L${OPT}/lib64"

./configure \
    --with-link-brotli=static \
    --with-link-gzip=static \
    --with-link-gzip-ng=static \
    --with-link-lz4=static \
    --with-link-snappy=static \
    --with-link-zstd=static

make NOLTO=1 STRIP=strip

# =============== test pg_z via "make installcheck" ================
export TMP_INST_DIR="$(pwd)/build/tmp_install"
export TMP_PGDATA="$(pwd)/build/tmp_pgdata"
export OPT_PG_DIR="/opt/homebrew/opt/postgresql@${PG_VERSION}"

make install DESTDIR="${TMP_INST_DIR}"

sudo cp ${TMP_INST_DIR}${OPT_PG_DIR}/lib/postgresql/pg_z.dylib ${OPT_PG_DIR}/lib/postgresql/
sudo cp ${TMP_INST_DIR}${OPT_PG_DIR}/share/postgresql/extension/pg_z* ${OPT_PG_DIR}/share/postgresql/extension/

# --- Starting DB ---
export PGPORT=54321
export PGUSER=postgres

initdb -D "${TMP_PGDATA}" --username=${PGUSER}
pg_ctl -D "${TMP_PGDATA}" -o "-p ${PGPORT}" -l postgres.log start
sleep 3

# --- actual tests ---
EXIT_CODE=0
if ! make installcheck; then
    if [ -f tmp/regression.diffs ]; then cat tmp/regression.diffs; fi
    EXIT_CODE=1
fi

# --- stop DB ---
pg_ctl -D "${TMP_PGDATA}" stop
[ "${EXIT_CODE}" != "0" ] && exit ${EXIT_CODE}

# =============== final packaging ================
RELEASE_STAGE="$(pwd)/build/macos_stage"
mkdir -p "${RELEASE_STAGE}/lib" "${RELEASE_STAGE}/extension"

cp ${OPT_PG_DIR}/lib/postgresql/pg_z.dylib ${RELEASE_STAGE}/lib/
cp ${OPT_PG_DIR}/share/postgresql/extension/pg_z* ${RELEASE_STAGE}/extension/

# --- stripping out debug information ---
strip -S "${RELEASE_STAGE}/lib/pg_z.dylib"

# --- Final stage ---
ARCHIVE_NAME="postgresql-${PG_VERSION}-pg-z_${PG_Z_VERSION}_macos_arm64.tar.gz"
tar -czf "${TARGET_DIR}/${ARCHIVE_NAME}" -C "${RELEASE_STAGE}" .

echo "Done! macOS package generated at ${TARGET_DIR}/${ARCHIVE_NAME}"
