AC_INIT([pg_z], [1.0])
AC_PREREQ([2.69])

AC_PROG_CC

# Detect target operating system
AC_CANONICAL_HOST
is_debian=no
if test -f /etc/debian_version; then
    is_debian=yes
fi


# ===========================================================================
# COMMAND LINE ARGUMENTS & OPTIONS FOR --help
# ===========================================================================

# Enable/Disable specific compression algorithms (default: yes)
AC_ARG_WITH([brotli],
            [AS_HELP_STRING([--with-brotli], [include Brotli support (yes/no) [default=yes]])],
            [with_brotli=$withval],
            [with_brotli=yes])
AC_ARG_WITH([gzip],
            [AS_HELP_STRING([--with-gzip],   [include Gzip/Deflate support (yes/no) [default=yes]])],
            [with_gzip=$withval],
            [with_gzip=yes])
AC_ARG_WITH([lz4],
            [AS_HELP_STRING([--with-lz4],    [include LZ4 support (yes/no) [default=yes]])],
            [with_lz4=$withval],
            [with_lz4=yes])
AC_ARG_WITH([snappy],
            [AS_HELP_STRING([--with-snappy], [include Snappy support (yes/no) [default=yes]])],
            [with_snappy=$withval],
            [with_snappy=yes])
AC_ARG_WITH([zstd],
            [AS_HELP_STRING([--with-zstd],   [include Zstandard support (yes/no) [default=yes]])],
            [with_zstd=$withval],
            [with_zstd=yes])

# Select Gzip optimization engine backend (zlib or zlib-ng)
AC_ARG_WITH([zlib-backend],
            [AS_HELP_STRING([--with-zlib-backend=zlib|zlib-ng], [select Gzip engine backend [default=zlib-ng]])],
            [with_zlib_backend=$withval],
            [with_zlib_backend=zlib-ng])

# Choose linking type (static or dynamic) for each library
AC_ARG_WITH([link-brotli],
            [AS_HELP_STRING([--with-link-brotli=static|dynamic], [linking type for Brotli [default: static]])],
            [BROTLI=$withval],
            [BROTLI=static])
AC_ARG_WITH([link-gzip],
            [AS_HELP_STRING([--with-link-gzip=static|dynamic],   [linking type for Gzip/Deflate [default=dynamic]])],
            [ZLIB=$withval],
            [ZLIB=dynamic])
AC_ARG_WITH([link-lz4],
            [AS_HELP_STRING([--with-link-lz4=static|dynamic],    [linking type for LZ4 [default=static]])],
            [LZ4=$withval],
            [LZ4=static])
AC_ARG_WITH([link-snappy],
            [AS_HELP_STRING([--with-link-snappy=static|dynamic], [linking type for Snappy [default: dynamic on Debian, static elsewhere]])],
            [SNAPPY=$withval],
            [SNAPPY=default])
AC_ARG_WITH([link-zstd],
            [AS_HELP_STRING([--with-link-zstd=static|dynamic],   [linking type for Zstandard [default=static]])],
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
], [
    AS_IF([test "x$SNAPPY" = "xdefault"], [SNAPPY=static])
])

# Validate Gzip combinations based on selected engine backend
AS_IF([test "x$with_gzip" = "xyes"], [
    AS_IF([test "x$with_zlib_backend" = "xzlib-ng"], [
        : # Zlib-ng allows both static and dynamic
    ], [
        # Standard legacy zlib can only be linked dynamically on Debian
        AS_IF([test "$ZLIB" = "static" && test "x$is_debian" = "xyes"], [
            AC_MSG_ERROR([Standard zlib can only be linked dynamically on Debian/Ubuntu due to missing -fPIC. Use --with-zlib-backend=zlib-ng for static linking, or set --with-link-gzip=dynamic])
        ])
    ])
])


SUPPORT_BROTLI="-- "
SUPPORT_ZLIB="-- "
SUPPORT_LZ4="-- "
SUPPORT_SNAPPY="-- "
SUPPORT_ZSTD="-- "

SYSTEM_LIB_DIR="/usr/lib/x86_64-linux-gnu"

# Variables to accumulate verified runtime libraries
DYNAMIC_LIBS=""
STATIC_LIBS=""
COMPRESSION_CFLAGS=""

# ===========================================================================
# REUSABLE MACROS FOR LIBRARY DETECTION
# ===========================================================================

# Reusable macro to check for dynamic libraries
AC_DEFUN([AC_CHECK_DYNAMIC_LIB], [
    AC_CHECK_LIB([$1], [$2], [
        AS_VAR_APPEND([DYNAMIC_LIBS], [" -l$1"])
    ], [
        AC_MSG_NOTICE([Library lib$1.so is missing. If its functionality is needed, install $3 development package.])
    ])
])

# Reusable macro to check for static libraries
AC_DEFUN([AC_CHECK_STATIC_LIB], [
    AC_CHECK_HEADERS([$1], [
        AS_IF([test -f "$SYSTEM_LIB_DIR/lib$2.a"], [
            AS_VAR_APPEND([STATIC_LIBS], [" -l$2"])
        ] , [
            AC_MSG_NOTICE([Library $2.a is missing. If its functionality is needed, install $2 development package.])
        ])
    ], [
        AC_MSG_NOTICE([$1 header is missing. Please install $2 development package])
    ])
])


# ===========================================================================
# CONDITIONAL ALGORITHM VERIFICATION
# ===========================================================================

# --- BROTLI ---
AS_IF([test "x$with_brotli" = "xyes"], [
    AS_IF([test "$BROTLI" = "static"], [
        AC_CHECK_STATIC_LIB([brotli/encode.h],            [brotlienc])
        AC_CHECK_STATIC_LIB([brotli/decode.h],            [brotlidec])
        AC_CHECK_STATIC_LIB([brotli/shared_dictionary.h], [brotlicommon])
    ], [
        AC_CHECK_DYNAMIC_LIB([brotlicommon], [BrotliSharedDictionaryCreateInstance], [libbrotli])
        AC_CHECK_DYNAMIC_LIB([brotlienc],    [BrotliEncoderCreateInstance],          [libbrotli])
        AC_CHECK_DYNAMIC_LIB([brotlidec],    [BrotliDecoderCreateInstance],          [libbrotli])
    ])
    COMPRESSION_CFLAGS="$COMPRESSION_CFLAGS -DUSE_brotli"
])

# --- GZIP / DEFLATE ---
AS_IF([test "x$with_gzip" = "xyes"], [
    AS_IF([test "x$with_zlib_backend" = "xzlib-ng"], [
        # Process hardware-accelerated Zlib-ng engine
        AS_IF([test "$ZLIB" = "static"], [
            AC_CHECK_STATIC_LIB([zlib.h], [z-zlib-ng])
        ], [
            AC_CHECK_LIB([z-zlib-ng], [zlibCompileFlags], [
                AS_VAR_APPEND([DYNAMIC_LIBS], [" -lz-zlib-ng"])
            ], [
                # Check fallback if zlib-ng is configured as transparent drop-in for libz
                AC_CHECK_DYNAMIC_LIB([z], [zlibCompileFlags], [zlib1g-dev-zlib-ng])
            ])
        ])
        if test -d /usr/include/zlib-ng; then COMPRESSION_CFLAGS="$COMPRESSION_CFLAGS -I/usr/include/zlib-ng"; fi
    ], [
        # Process standard legacy Zlib engine
        AS_IF([test "$ZLIB" = "static"], [
            AC_CHECK_STATIC_LIB([zlib.h], [z])
        ], [
            AC_CHECK_DYNAMIC_LIB([z], [deflate], [zlib])
        ])
    ])
    COMPRESSION_CFLAGS="$COMPRESSION_CFLAGS -DUSE_gzip"
])

# --- LZ4 ---
AS_IF([test "x$with_lz4" = "xyes"], [
    AS_IF([test "$LZ4" = "static"], [
        AC_CHECK_STATIC_LIB([lz4.h], [lz4])
    ], [
        AC_CHECK_DYNAMIC_LIB([lz4], [LZ4_compress_default], [liblz4])
    ])
    COMPRESSION_CFLAGS="$COMPRESSION_CFLAGS -DUSE_lz4"
])

# --- SNAPPY ---
AS_IF([test "x$with_snappy" = "xyes"], [
    AS_IF([test "$SNAPPY" = "static"], [
        AC_CHECK_STATIC_LIB([snappy.h], [snappy])
    ], [
        AC_CHECK_DYNAMIC_LIB([snappy], [snappy_compress], [libsnappy])
    ])
    COMPRESSION_CFLAGS="$COMPRESSION_CFLAGS -DUSE_snappy"
])

# --- ZSTANDARD ---
AS_IF([test "x$with_zstd" = "xyes"], [
    AS_IF([test "$ZSTD" = "static"], [
        AC_CHECK_STATIC_LIB([zstd.h], [zstd])
    ], [
        AC_CHECK_DYNAMIC_LIB([zstd], [ZSTD_compress], [libzstd])
    ])
    COMPRESSION_CFLAGS="$COMPRESSION_CFLAGS -DUSE_zstd"
])


# ===========================================================================
# CONDITIONAL FEATURES METADATA PARSING
# ===========================================================================

ALL_LIBS=" $DYNAMIC_LIBS $STATIC_LIBS "
case "$ALL_LIBS" in
    *" -lbrotli"*) SUPPORT_BROTLI="" ;;
esac

case "$ALL_LIBS" in
    *" -lz "*|*" -lz-zlib-ng"*) SUPPORT_ZLIB="" ;;
esac

case "$ALL_LIBS" in
    *" -llz4"*)    SUPPORT_LZ4="" ;;
esac

case "$ALL_LIBS" in
    *" -lsnappy"*) SUPPORT_SNAPPY="" ;;
esac

case "$ALL_LIBS" in
    *" -lzstd"*)   SUPPORT_ZSTD="" ;;
esac


# Export results
AC_SUBST(DYNAMIC_LIBS)
AC_SUBST(STATIC_LIBS)
AC_SUBST(COMPRESSION_CFLAGS)
AC_SUBST(SUPPORT_BROTLI)
AC_SUBST(SUPPORT_ZLIB)
AC_SUBST(SUPPORT_LZ4)
AC_SUBST(SUPPORT_SNAPPY)
AC_SUBST(SUPPORT_ZSTD)

CONFIGURE_RUN="yes"
AC_SUBST(CONFIGURE_RUN)

AC_MSG_NOTICE([==================================================])
AC_MSG_NOTICE([STATIC_LIBS:   $STATIC_LIBS])
AC_MSG_NOTICE([DYNAMIC_LIBS:  $DYNAMIC_LIBS])
AC_MSG_NOTICE([CFLAGS MACROS: $COMPRESSION_CFLAGS])
AC_MSG_NOTICE([==================================================])

AC_CONFIG_FILES([Makefile.port])
AC_OUTPUT
