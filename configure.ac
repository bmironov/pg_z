AC_INIT([pg_z], [1.0])
AC_PREREQ([2.69])

AC_PROG_CC

# Detect target operating system
AC_CANONICAL_HOST
is_debian=no
if test -f /etc/debian_version; then
    is_debian=yes
fi

AC_CHECK_LIB([m], [cos]) # Required for static linking of Brotli

# ===========================================================================
# COMMAND LINE ARGUMENTS & OPTIONS FOR --help
# ===========================================================================

# Enable/Disable specific compression algorithms (default: yes)
AC_ARG_WITH([brotli],
            [AS_HELP_STRING([--with-brotli],  [include Brotli support (yes/no) [default=yes]])],
            [with_brotli=$withval],
            [with_brotli=yes])
AC_ARG_WITH([gzip],
            [AS_HELP_STRING([--with-gzip],    [include Gzip/Deflate support via standard zlib (yes/no) [default=yes]])],
            [with_gzip=$withval],
            [with_gzip=yes])
AC_ARG_WITH([gzip-ng],
            [AS_HELP_STRING([--with-gzip-ng], [include Gzip/Deflate support via zlib-ng (yes/no) [default=yes]])],
            [with_gzip_ng=$withval],
            [with_gzip_ng=yes])
AC_ARG_WITH([lz4],
            [AS_HELP_STRING([--with-lz4],     [include LZ4 support (yes/no) [default=yes]])],
            [with_lz4=$withval],
            [with_lz4=yes])
AC_ARG_WITH([snappy],
            [AS_HELP_STRING([--with-snappy],  [include Snappy support (yes/no) [default=yes]])],
            [with_snappy=$withval],
            [with_snappy=yes])
AC_ARG_WITH([zstd],
            [AS_HELP_STRING([--with-zstd],    [include Zstandard support (yes/no) [default=yes]])],
            [with_zstd=$withval],
            [with_zstd=yes])

# Choose linking type (static or dynamic) for each library
AC_ARG_WITH([link-brotli],
            [AS_HELP_STRING([--with-link-brotli=static|dynamic],  [linking type for Brotli [default: static]])],
            [BROTLI=$withval],
            [BROTLI=static])
AC_ARG_WITH([link-gzip],
            [AS_HELP_STRING([--with-link-gzip=static|dynamic],    [linking type for Gzip/Deflate (zlib) [default=dynamic]])],
            [ZLIB=$withval],
            [ZLIB=dynamic])
AC_ARG_WITH([link-gzip-ng],
            [AS_HELP_STRING([--with-link-gzip-ng=static|dynamic], [linking type for Gzip/Deflate (zlib-ng) [default=static]])],
            [ZLIB_NG=$withval],
            [ZLIB_NG=static])
AC_ARG_WITH([link-lz4],
            [AS_HELP_STRING([--with-link-lz4=static|dynamic],     [linking type for LZ4 [default=static]])],
            [LZ4=$withval],
            [LZ4=static])
AC_ARG_WITH([link-snappy],
            [AS_HELP_STRING([--with-link-snappy=static|dynamic],  [linking type for Snappy [default: dynamic on Debian, static elsewhere]])],
            [SNAPPY=$withval],
            [SNAPPY=default])
AC_ARG_WITH([link-zstd],
            [AS_HELP_STRING([--with-link-zstd=static|dynamic],    [linking type for Zstandard [default=static]])],
            [ZSTD=$withval],
            [ZSTD=static])


# ===========================================================================
# VALIDATE LINKING CONSTRAINTS
# ===========================================================================

# Resolve smart defaults based on OS family
AS_IF([test "x$is_debian" = "xyes"], [
    case "$SNAPPY" in
        default) SNAPPY=dynamic ;;
        static)  AC_MSG_ERROR([Snappy can only be linked dynamically on Debian/Ubuntu due to missing static archives. Please use --with-link-snappy=dynamic]) ;;
    esac
    case "$ZLIB" in
        default) ZLIB=dynamic ;;
        static) AC_MSG_ERROR([Standard zlib can only be linked dynamically on Debian/Ubuntu due to missing -fPIC. Please use --with-link-gzip=dynamic]) ;;
    esac
], [
    AS_IF([test "x$SNAPPY" = "xdefault"], [SNAPPY=static])
])


SYSTEM_LIB_DIR="/usr/lib/x86_64-linux-gnu"

# Variables to accumulate verified runtime libraries
STATIC_LIBS=""
DYNAMIC_LIBS=""
COMPRESSION_CFLAGS=""
ALGOS=""

# ===========================================================================
# REUSABLE MACROS FOR LIBRARY DETECTION
# ===========================================================================

# Reusable macro to check for dynamic libraries
# Parameters:
# $1 flag variable suffix (eg, zlib_ng)
# $2 common name of the library (eg, 'zlib-ng')
# $3 official library name on the filesystem (as lib$2,so, or 'z-ng' for zlib-ng)
# $4 function defined by library (eg, zng_deflate)
AC_DEFUN([MY_CHECK_LIB], [
    AS_VAR_SET([HAVE_$1], [no])
    LIBS="-l$3 $LIBS"

    AS_IF([test "$MODE" = "static"], [
        save_LDFLAGS="$LDFLAGS"
        LDFLAGS="$LDFLAGS -static"

        AC_CHECK_LIB([$3], [$4], [
            AS_VAR_APPEND([STATIC_LIBS], [" -l$3"])
            AS_VAR_SET([HAVE_$1], [yes])
        ], [
            AC_MSG_NOTICE([Library lib$3.a is missing. If its functionality is needed, install $1 development package.])
        ])

        LDFLAGS="$save_LDFLAGS"
    ], [
        AC_CHECK_LIB([$3], [$4], [
            AS_VAR_APPEND([DYNAMIC_LIBS], [" -l$3"])
            AS_VAR_SET([HAVE_$1], [yes])
        ], [
            AC_MSG_NOTICE([Library lib$3.so is missing. If its functionality is needed, install $1 development package.])
        ])
    ])
])


# ===========================================================================
# CONDITIONAL ALGORITHM VERIFICATION
# ===========================================================================

# --- Brotli ---
AS_IF([test "x$with_brotli" = "xyes"], [
    MODE="$BROTLI"
    MY_CHECK_LIB([brotli_c], [libbrotli], [brotlicommon], [BrotliSharedDictionaryCreateInstance])
    MY_CHECK_LIB([brotli_e], [libbrotli], [brotlienc],    [BrotliEncoderCreateInstance])
    MY_CHECK_LIB([brotli_d], [libbrotli], [brotlidec],    [BrotliDecoderCreateInstance])
    AS_IF([test "x$HAVE_brotli_c" = "xyes" -a "x$HAVE_brotli_e" = "xyes" -a "x$HAVE_brotli_d" = "xyes"], [
        AS_VAR_APPEND([COMPRESSION_CFLAGS], [" -DUSE_brotli"])
        AS_VAR_APPEND([ALGOS], [" Brotli"])
    ])
])

# --- Gzip / Deflate ---
# Process standard legacy Zlib engine
AS_IF([test "x$with_gzip" = "xyes"], [
    MODE="$ZLIB"
    MY_CHECK_LIB([zlib], [zlib], [z], [deflate])
    AS_IF([test "x$HAVE_zlib" = "xyes"], [
        AS_VAR_APPEND([COMPRESSION_CFLAGS], [" -DUSE_gzip"])
        AS_VAR_APPEND([ALGOS], [" Gzip/Deflate"])
    ])
])

# Process Zlib-NG engine
AS_IF([test "x$with_gzip_ng" = "xyes"], [
    MODE="$ZLIB_NG"
    MY_CHECK_LIB([zlib_ng], [libz-ng], [z-ng], [zng_deflate])
    AS_IF([test "x$HAVE_zlib_ng" = "xyes"], [
        AS_VAR_APPEND([COMPRESSION_CFLAGS], [" -DUSE_gzip_ng"])
        AS_VAR_APPEND([ALGOS], [" Gzip-NG/Deflate-NG"])
    ])
])

# --- Lz4 ---
AS_IF([test "x$with_lz4" = "xyes"], [
    MODE="$LZ4"
    MY_CHECK_LIB([lz4], [liblz4], [lz4], [LZ4_compress_default])
    AS_IF([test "x$HAVE_lz4" = "xyes"], [
        AS_VAR_APPEND([COMPRESSION_CFLAGS], [" -DUSE_lz4"])
        AS_VAR_APPEND([ALGOS], [" LZ4"])
    ])
])

# --- Snappy ---
AS_IF([test "x$with_snappy" = "xyes"], [
    MODE="$SNAPPY"
    MY_CHECK_LIB([snappy], [libsnappy], [snappy], [snappy_compress])
    AS_IF([test "x$HAVE_snappy" = "xyes"], [
        AS_VAR_APPEND([COMPRESSION_CFLAGS], [" -DUSE_snappy"])
        AS_VAR_APPEND([ALGOS], [" Snappy"])
    ])
])

# --- Zstandard ---
AS_IF([test "x$with_zstd" = "xyes"], [
    MODE="$ZSTD"
    MY_CHECK_LIB([zstd], [libzstd], [zstd], [ZSTD_compress])
    AS_IF([test "x$HAVE_zstd" = "xyes"], [
        AS_VAR_APPEND([COMPRESSION_CFLAGS], [" -DUSE_zstd"])
        AS_VAR_APPEND([ALGOS], [" Zstd"])
    ])
])


CONFIGURE_RUN="yes"
ALGO_LIST=$(echo "$ALGOS" | xargs | sed "s/ /, /g")

if test -f VERSION; then
    GIT_VERSION=$(cat VERSION | tr -d '\n')
elif test -d .git; then
    GIT_VERSION=$(git describe --tags --abbrev=0 2>/dev/null)
else
    GIT_VERSION="0.0.1"
fi

# Export results
AC_SUBST(DYNAMIC_LIBS)
AC_SUBST(STATIC_LIBS)
AC_SUBST(COMPRESSION_CFLAGS)
AC_SUBST(CONFIGURE_RUN)
AC_SUBST([GIT_VERSION])
AC_SUBST([ALGO_LIST])

AC_MSG_NOTICE([==================================================])
AC_MSG_NOTICE([GIT_VERSION:   $GIT_VERSION])
AC_MSG_NOTICE([ALGO_LIST:     $ALGO_LIST])
AC_MSG_NOTICE([STATIC_LIBS:   $STATIC_LIBS])
AC_MSG_NOTICE([DYNAMIC_LIBS:  $DYNAMIC_LIBS])
AC_MSG_NOTICE([CFLAGS MACROS: $COMPRESSION_CFLAGS])
AC_MSG_NOTICE([==================================================])

AC_CONFIG_FILES([Makefile.port tmp/Makefile pg_z.control])
AC_OUTPUT
