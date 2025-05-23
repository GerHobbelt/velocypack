# -*- mode: CMAKE; -*-

cmake_minimum_required(VERSION 3.21)
project(velocypack CXX C)

set(CMAKE_CXX_STANDARD 20 CACHE STRING "target C++ standard")
message(STATUS "Require C++${CMAKE_CXX_STANDARD}")
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_CURRENT_SOURCE_DIR}/cmake/Modules")

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(UseIPO "AUTO" CACHE STRING "Use interprocedural optimization: ON, OFF or AUTO")
set_property(CACHE UseIPO PROPERTY STRINGS AUTO ON OFF)

# Determine value if IPO_ENABLED from USE_IPO and CMAKE_BUILD_TYPE
set(IPO_ENABLED False)
if (UseIPO STREQUAL "AUTO")
  if (CMAKE_BUILD_TYPE STREQUAL "Release"
      OR CMAKE_BUILD_TYPE STREQUAL "RelWithDebInfo"
      OR CMAKE_BUILD_TYPE STREQUAL "MinSizeRel")
    set(IPO_ENABLED True)
  else()
    set(IPO_ENABLED False)
  endif ()
elseif (UseIPO)
  set(IPO_ENABLED True)
else()
  set(IPO_ENABLED False)
endif()

if (IPO_ENABLED)
  set(CMAKE_INTERPROCEDURAL_OPTIMIZATION True)
endif()

option(Maintainer "Build maintainer tools" OFF)
option(BuildSseOpt "Build with SSE optimization instructions" ON)

set(HashType "xxhash" CACHE STRING "Hash type (fasthash, xxhash)" )

# Set Build Type
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Choose the type of build, options are: None Debug Release RelWithDebInfo MinSizeRel." FORCE)
endif()

message(STATUS "VelocyPack build mode: ${CMAKE_BUILD_TYPE}")
message(STATUS "VelocyPack interprocedural optimizations: ${IPO_ENABLED}")
message(STATUS "VelocyPack install base directory: ${CMAKE_INSTALL_PREFIX}")
set(VELOCYPACK_VERSION "0.1.33" CACHE STRING "VelocyPack version")
set(VELOCYPACK_DISPLAY_NAME "Velocypack")
set(VELOCYPACK_URL_INFO_ABOUT "https://github.com/arangodb/velocypack")
set(VELOCYPACK_CONTACT "hackers@arangodb.org")
set(VELOCYPACK_FRIENDLY_STRING "VelocyPack - A fast & compact serialization format")

set(CMAKE_POSITION_INDEPENDENT_CODE ON)

#Compiler Setting
include(AR_CompilerSettings)
if(NOT BuildSseOpt OR NOT CAN_USE_SSE)
  add_definitions("-DVELOCYPACK_ASM_OPTIMIZATIONS=0")
else()
  if(ADD_SSE_FLAG)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -msse4.2")
  elseif(IS_ARM)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -D__ARM_NEON")
  endif()
  add_definitions("-DVELOCYPACK_ASM_OPTIMIZATIONS=1")
endif()

add_library(velocypack STATIC
  src/velocypack-common.cpp
  src/AttributeTranslator.cpp
  src/Builder.cpp
  src/Collection.cpp
  src/Compare.cpp
  src/Dumper.cpp
  src/Exception.cpp
  src/HashedStringRef.cpp
  src/HexDump.cpp
  src/Iterator.cpp
  src/Options.cpp
  src/Parser.cpp
  src/Serializable.cpp
  src/SharedSlice.cpp
  src/Slice.cpp
  src/Utf8Helper.cpp
  src/Validator.cpp
  src/Value.cpp
  src/ValueType.cpp
  src/Version.cpp
  src/asm-functions.cpp
  src/asm-utf8check.cpp
  src/fpconv.cpp
  src/string-functions.cpp
        src/String.cpp)

#Use xxhash, fasthash?
if(HashType STREQUAL "xxhash")
  target_sources(velocypack PRIVATE src/xxhash.c)
  target_compile_definitions(velocypack PRIVATE "-DXXH_NAMESPACE=VELOCYPACK_")
  add_definitions("-DVELOCYPACK_XXHASH=1")
elseif(HashType STREQUAL "fasthash")
  target_sources(velocypack PRIVATE src/fasthash.cpp)
  add_definitions("-DVELOCYPACK_FASTHASH=1")
else()
  message(FATAL_ERROR "VelocyPack invalid HashType value. supported values: xxhash, fasthash")
endif()
message(STATUS "VelocyPack Building with hash type: ${HashType}")

target_include_directories(
  velocypack
  PRIVATE ${PROJECT_SOURCE_DIR}/src
  PUBLIC ${PROJECT_SOURCE_DIR}/include
)
 
if(IS_ARM)
  target_include_directories(velocypack PRIVATE ${PROJECT_SOURCE_DIR}/external/sse2neon)
endif()

if(Maintainer)
  add_executable(buildVersion scripts/build-version.cpp)
  add_custom_target(buildVersionNumber
    COMMAND ${PROJECT_BINARY_DIR}/scripts/buildVersion
    ${PROJECT_SOURCE_DIR}/include/velocypack/velocypack-version-number.h
    DEPENDS ${PROJECT_BINARY_DIR}/scripts/buildVersion
    VERBATIM
  )
endif()

add_subdirectory(tools)
add_subdirectory(examples)
add_subdirectory(tests)

install(TARGETS velocypack DESTINATION lib)
install(
  DIRECTORY   "include/velocypack"
  DESTINATION include
)
include(CPack)
