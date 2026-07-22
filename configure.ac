AC_INIT([pg_z], [1.0])
AC_PREREQ([2.69])

AC_PROG_CC

# Variable to accumulate verified runtime libraries
DYNAMIC_LIBS=""

# Define a reusable macro to check for required dynamic libraries
# Arguments:
#   $1: library name (e.g., z, lz4, zstd)
#   $2: function name to test (e.g., deflate, LZ4_compress_default)
#   $3: suggest package name for error message (e.g., zlib, liblz4)
AC_DEFUN([AC_CHECK_REQUIRED_LIB], [
    AC_CHECK_LIB([$1], [$2], [
        DYNAMIC_LIBS="$DYNAMIC_LIBS -l$1"
    ], [
        AC_MSG_ERROR([Required library lib$1.so is missing. Please install $3 development package.])
    ])
])

# Call the macro for each required library
AC_CHECK_REQUIRED_LIB([brotlicommon], [BrotliSharedDictionaryCreateInstance], [libbrotli])
AC_CHECK_REQUIRED_LIB([brotlienc], [BrotliEncoderCreateInstance], [libbrotli])
AC_CHECK_REQUIRED_LIB([brotlidec], [BrotliDecoderCreateInstance], [libbrotli])
AC_CHECK_REQUIRED_LIB([z], [deflate], [zlib])
AC_CHECK_REQUIRED_LIB([lz4], [LZ4_compress_default], [liblz4])
AC_CHECK_REQUIRED_LIB([zstd], [ZSTD_compress], [libzstd])


SUPPORT_BROTLI="-- "
SUPPORT_ZLIB="-- "
SUPPORT_LZ4="-- "
SUPPORT_ZSTD="-- "

case "$DYNAMIC_LIBS" in
    *-lbrotli*) SUPPORT_BROTLI="" ;;
esac

case "$DYNAMIC_LIBS" in
    *-lz*)      SUPPORT_ZLIB="" ;;
esac

case "$DYNAMIC_LIBS" in
    *-llz4*)    SUPPORT_LZ4="" ;;
esac

case "$DYNAMIC_LIBS" in
    *-lzstd*)   SUPPORT_ZSTD="" ;;
esac


# Export results to Makefile.port
AC_SUBST(DYNAMIC_LIBS)
AC_SUBST(SUPPORT_BROTLI)
AC_SUBST(SUPPORT_ZLIB)
AC_SUBST(SUPPORT_LZ4)
AC_SUBST(SUPPORT_ZSTD)

CONFIGURE_RUN="yes"
AC_SUBST(CONFIGURE_RUN)

AC_CONFIG_FILES([Makefile.port pg_z--1.0.sql])
AC_OUTPUT

