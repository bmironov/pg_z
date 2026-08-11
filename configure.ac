AC_INIT([pg_z], [1.0])
AC_PREREQ([2.69])

AC_PROG_CC

# Set compilation type for each library
BROTLI="static"
ZLIB="dynamic" # Should be dynamic because Debian supplies zlib without -fPIC flag
LZ4="static"
SNAPPY="dynamic" # Should be dynamic because it's C++ library and .a isn't provided
ZSTD="static"

SUPPORT_BROTLI="-- "
SUPPORT_ZLIB="-- "
SUPPORT_LZ4="-- "
SUPPORT_SNAPPY="-- "
SUPPORT_ZSTD="-- "


SYSTEM_LIB_DIR="/usr/lib/x86_64-linux-gnu"


# Variable to accumulate verified runtime libraries
DYNAMIC_LIBS=""
STATIC_LIBS=""

# Define a reusable macro to check for dynamic libraries
# Arguments:
#   $1: library name (e.g., z, lz4, zstd)
#   $2: function name to test (e.g., deflate, LZ4_compress_default)
#   $3: suggest package name for error message (e.g., zlib, liblz4)
AC_DEFUN([AC_CHECK_DYNAMIC_LIB], [
    AC_CHECK_LIB([$1], [$2], [
        AS_VAR_APPEND([DYNAMIC_LIBS], [" -l$1"])
    ], [
        AC_MSG_NOTICE([Library lib$1.so is missing. If its functionality is needed, install $3 development package.])
    ])
])

# Define reusable macro to check for stic libraries
# Arguments:
#   $1: header file (e.g., zstd.h)
#   $2: library static file (e.g., libzstd.a)
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


# Call the macro for each required library
AS_IF([test "$BROTLI" = "static"], [
    AC_CHECK_STATIC_LIB([brotli/encode.h],            [brotlienc])
    AC_CHECK_STATIC_LIB([brotli/decode.h],            [brotlidec])
    AC_CHECK_STATIC_LIB([brotli/shared_dictionary.h], [brotlicommon])
], [
    AC_CHECK_DYNAMIC_LIB([brotlicommon], [BrotliSharedDictionaryCreateInstance], [libbrotli])
    AC_CHECK_DYNAMIC_LIB([brotlienc],    [BrotliEncoderCreateInstance],          [libbrotli])
    AC_CHECK_DYNAMIC_LIB([brotlidec],    [BrotliDecoderCreateInstance],          [libbrotli])
])

AS_IF([test "$ZLIB" = "static"], [
    AC_CHECK_STATIC_LIB([zlib.h], [z])
], [
    AC_CHECK_DYNAMIC_LIB([z], [deflate], [zlib])
])

AS_IF([test "$LZ4" = "static"], [
    AC_CHECK_STATIC_LIB([lz4.h], [lz4])
], [
    AC_CHECK_DYNAMIC_LIB([lz4], [LZ4_compress_default], [liblz4])
])

AC_CHECK_DYNAMIC_LIB([snappy], [snappy_compress], [libsnappy])

AS_IF([test "$ZSTD" = "static"], [
    AC_CHECK_STATIC_LIB([zstd.h], [zstd])
], [
    AC_CHECK_DYNAMIC_LIB([zstd], [ZSTD_compress], [libzstd])
])


ALL_LIBS=" $DYNAMIC_LIBS $STATIC_LIBS "
case "$ALL_LIBS" in
    *" -lbrotli"*) SUPPORT_BROTLI="" ;;
esac

case "$ALL_LIBS" in
    *" -lz "*)     SUPPORT_ZLIB="" ;;
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


# Export results to Makefile.port
AC_SUBST(DYNAMIC_LIBS)
AC_SUBST(STATIC_LIBS)
AC_SUBST(SUPPORT_BROTLI)
AC_SUBST(SUPPORT_ZLIB)
AC_SUBST(SUPPORT_LZ4)
AC_SUBST(SUPPORT_SNAPPY)
AC_SUBST(SUPPORT_ZSTD)

CONFIGURE_RUN="yes"
AC_SUBST(CONFIGURE_RUN)

AC_MSG_NOTICE([==================================================])
AC_MSG_NOTICE([STATIC_LIBS:  $STATIC_LIBS])
AC_MSG_NOTICE([DYNAMIC_LIBS: $DYNAMIC_LIBS])
AC_MSG_NOTICE([==================================================])

AC_CONFIG_FILES([Makefile.port pg_z--1.0.sql])
AC_OUTPUT
