# ==========================================
# STAGE 1
# ==========================================
FROM debian:13-slim AS builder

ARG ZLIB_NG_VERSION=2.3.3

RUN apt-get update && apt-get install -y \
    build-essential \
    cmake \
    git \
    wget \
    tar \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /usr/src

RUN wget https://github.com/zlib-ng/zlib-ng/archive/refs/tags/${ZLIB_NG_VERSION}.tar.gz \
    && tar -xzf ${ZLIB_NG_VERSION}.tar.gz \
    && mv zlib-ng-${ZLIB_NG_VERSION} zlib-ng \
    && rm ${ZLIB_NG_VERSION}.tar.gz

# --- Build Variant 1: Native Mode (-lz-ng) ---
WORKDIR /usr/src/zlib-ng/build
RUN cmake .. \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
    -DCMAKE_INSTALL_PREFIX=/opt/zlib-ng-native

RUN cmake --build . --parallel $(nproc)
RUN cmake --install .

# --- Build Variant 2: Compat Mode (-lz) ---
WORKDIR /usr/src/zlib-ng/build-compat
RUN cmake .. \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
    -DZLIB_COMPAT=ON \
    -DCMAKE_INSTALL_PREFIX=/opt/zlib-ng-compat

RUN cmake --build . --parallel $(nproc)
RUN cmake --install .

# ==========================================
# STAGE 2
# ==========================================
FROM debian:13-slim

COPY --from=builder /opt/zlib-ng-native /opt/zlib-ng-native
COPY --from=builder /opt/zlib-ng-compat /opt/zlib-ng-compat

WORKDIR /workspace

CMD ["bash"]

