# ============================================================
# LongPet - LoongArch64 / Loongson 2K0300
# Buildroot SDK Toolchain
# ============================================================

set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR loongarch64)

# ------------------------------------------------------------
# Buildroot SDK
# ------------------------------------------------------------

if(DEFINED ENV{LOONGARCH_SDK_ROOT})
    set(SDK_ROOT "$ENV{LOONGARCH_SDK_ROOT}")
else()
    set(
        SDK_ROOT
        "/opt/loongarch64-buildroot-linux-gnu_sdk-buildroot"
    )
endif()

# ------------------------------------------------------------
# Cross compiler
# ------------------------------------------------------------

set(
    CMAKE_C_COMPILER
    "${SDK_ROOT}/bin/loongarch64-loongson-linux-gnu-gcc"
)

set(
    CMAKE_CXX_COMPILER
    "${SDK_ROOT}/bin/loongarch64-loongson-linux-gnu-g++"
)

# ------------------------------------------------------------
# Target sysroot
# ------------------------------------------------------------

set(
    CMAKE_SYSROOT
    "${SDK_ROOT}/loongarch64-buildroot-linux-gnu/sysroot"
)

set(
    CMAKE_FIND_ROOT_PATH
    "${CMAKE_SYSROOT}"
)

# ------------------------------------------------------------
# CMake package lookup policy
#
# PROGRAM:
#   moc/rcc/cmake/ninja 等必须在 x86_64 WSL 上运行
#
# LIBRARY/INCLUDE/PACKAGE:
#   必须从 LoongArch target sysroot 中查找
# ------------------------------------------------------------

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# ------------------------------------------------------------
# Qt6 target package
# ------------------------------------------------------------

set(
    Qt6_DIR
    "${CMAKE_SYSROOT}/usr/lib/cmake/Qt6"
    CACHE PATH
    "Target Qt6 CMake directory"
)

# ------------------------------------------------------------
# Qt6 host tools
#
# SDK_ROOT 下存在：
#   libexec/moc
#   libexec/rcc
#   bin/qtpaths
#   bin/qmake
# ------------------------------------------------------------

set(
    QT_HOST_PATH
    "${SDK_ROOT}"
    CACHE PATH
    "Qt host tools prefix"
)

# ------------------------------------------------------------
# Do not accidentally search Windows/host target libraries
# ------------------------------------------------------------

set(CMAKE_INSTALL_PREFIX "/usr" CACHE PATH "" FORCE)