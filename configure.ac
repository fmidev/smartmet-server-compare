AC_PREREQ([2.69])
AC_INIT([smartmet-server-compare], [26.4.17], [andris.pavenis@fmi.fi])
AC_CONFIG_SRCDIR([compare/main.cpp])
AC_CONFIG_AUX_DIR([build-aux])
AC_CONFIG_MACRO_DIR([m4])
AM_INIT_AUTOMAKE([foreign subdir-objects -Wall -Werror])

AC_PROG_CXX
AC_LANG([C++])
AX_CXX_COMPILE_STDCXX([17], [noext], [mandatory])

# Required pkg-config modules
PKG_CHECK_MODULES([GTKMM], [gtkmm-3.0])
PKG_CHECK_MODULES([MAGICK], [Magick++])
PKG_CHECK_MODULES([CURL], [libcurl])

# Libraries without pkg-config
AC_CHECK_LIB([tinyxml2], [main], [], [AC_MSG_ERROR([libtinyxml2 not found])])
AC_CHECK_LIB([jsoncpp], [main], [], [AC_MSG_ERROR([libjsoncpp not found])])

AC_CONFIG_FILES([Makefile])
AC_OUTPUT
