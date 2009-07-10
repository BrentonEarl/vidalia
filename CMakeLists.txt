##
##  $Id$
## 
##  This file is part of Vidalia, and is subject to the license terms in the
##  LICENSE file, found in the top level directory of this distribution. If 
##  you did not receive the LICENSE file with this file, you may obtain it
##  from the Vidalia source package distributed by the Vidalia Project at
##  http://www.vidalia-project.net/. No part of Vidalia, including this file,
##  may be copied, modified, propagated, or distributed except according to
##  the terms described in the LICENSE file.
##


set(VER_MAJOR   "0")
set(VER_MINOR   "1")
set(VER_PATCH   "15")
set(VERSION     "${VER_MAJOR}.${VER_MINOR}.${VER_PATCH}")
message(STATUS  "Configuring Vidalia ${VERSION}")
project(Vidalia)

## Specify the minimim required CMake version
cmake_minimum_required(VERSION 2.4.0)
if (COMMAND cmake_policy)
  # Force CMake 2.4 compatibility for handling linker search paths
  cmake_policy(SET CMP0003 OLD)
endif(COMMAND cmake_policy)

## Require Qt >= 4.3.0
set(QT_MIN_VERSION    "4.3.0")

## Specify the Qt libraries used
include(FindQt4)
find_package(Qt4 REQUIRED)
set(QT_USE_QTNETWORK  true)
set(QT_USE_QTXML      true)
include(${QT_USE_FILE})
include(${CMAKE_SOURCE_DIR}/cmake/VidaliaMacros.cmake)
include(CheckIncludeFile)
include(CheckIncludeFileCXX)
include(CheckTypeSize)
include(CPack)
if (MSVC OR UNIX)
  include(${CMAKE_SOURCE_DIR}/cmake/FindOpenSSL.cmake)
endif(MSVC OR UNIX)

if(MSVC_IDE)
    set(CMAKE_SUPPRESS_REGENERATION TRUE)
    set(LIBRARY_OUTPUT_PATH ${CMAKE_BINARY_DIR}/bin)
    set(EXECUTABLE_OUTPUT_PATH ${CMAKE_BINARY_DIR}/bin)
endif(MSVC_IDE)
  
## Define Vidalia-specific CMake options
if (APPLE)
  option(OSX_FAT_BINARY "Build Vidalia as a Universal binary." OFF)
  if (OSX_FAT_BINARY)
    set(CMAKE_OSX_ARCHITECTURES "ppc;i386" 
        CACHE STRING "OS X build architectures" FORCE)
    set(CMAKE_EXE_LINKER_FLAGS "-mmacosx-version-min=10.4"
        CACHE STRING "Flags used by the linker." FORCE)
    set(CMAKE_OSX_SYSROOT "/Developer/SDKs/MacOSX10.4u.sdk"
        CACHE STRING "isysroot used for universal binary support" FORCE)
  endif(OSX_FAT_BINARY)
endif(APPLE)

## UPnP support is currently optional (enabled by default)
option(USE_MINIUPNPC "Enable UPnP support using the MiniUPnPc library." ON)

## Check for system header files
check_include_file("limits.h" HAVE_LIMITS_H)
check_include_file("sys/limits.h" HAVE_SYS_LIMITS_H)

## Check for the sizes of various data types
check_type_size(int SIZEOF_INT)

## Write out a configuration file
configure_file(
  ${CMAKE_CURRENT_SOURCE_DIR}/config.h.in
  ${CMAKE_CURRENT_BINARY_DIR}/config.h
)
include_directories(${CMAKE_CURRENT_BINARY_DIR})

## Add the actual source directories
add_subdirectory(src)
add_subdirectory(doc)
add_subdirectory(pkg)

