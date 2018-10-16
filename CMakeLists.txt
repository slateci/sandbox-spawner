cmake_minimum_required (VERSION 3.0.0 FATAL_ERROR)
cmake_policy(VERSION 3.0.0)

SET(CMAKE_CXX_STANDARD 11)
SET(CMAKE_C_STANDARD 11)

# -----------------------------------------------------------------------------
# Set up installation
# Install to correct subdirectories on all platforms
include(GNUInstallDirs)
if(APPLE)
  # See: https://cmake.org/Wiki/CMake_RPATH_handling
  set(CMAKE_MACOSX_RPATH TRUE)
  set(CMAKE_SKIP_BUILD_RPATH FALSE)
  set(CMAKE_BUILD_WITH_INSTALL_RPATH TRUE)
  set(CMAKE_INSTALL_RPATH_USE_LINK_PATH TRUE)
  set(CMAKE_INSTALL_RPATH "${CMAKE_INSTALL_PREFIX}/lib")
endif(APPLE)

# -----------------------------------------------------------------------------
# Look for dependencies
SET (CMAKE_MODULE_PATH ${CMAKE_SOURCE_DIR}/cmake/Packages)
FIND_PACKAGE(Boost COMPONENTS date_time serialization system)
FIND_PACKAGE(libcrypto)
FIND_PACKAGE(ssl)
FIND_PACKAGE(CURL)

# -----------------------------------------------------------------------------
# Main executable
LIST(APPEND SERVICE_SOURCES
  ${CMAKE_SOURCE_DIR}/src/sandbox_spawner.cpp
  ${CMAKE_SOURCE_DIR}/src/Process.cpp
  ${CMAKE_SOURCE_DIR}/src/Utilities.cpp
  ${CMAKE_SOURCE_DIR}/src/HTTPRequests.cpp
  ${CMAKE_SOURCE_DIR}/src/base64.cpp
)

SET(SLATE_SERVER_COMPILE_OPTIONS 
  ${CURL_CFLAGS}
  ${LIBCRYPTO_CFLAGS}
  ${SSL_CFLAGS}
  -DRAPIDJSON_HAS_STDSTRING
  -O2
)

add_executable(sandbox-spawner ${SERVICE_SOURCES})
target_include_directories(sandbox-spawner
  PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>
  ${Boost_INCLUDE_DIRS}
)
target_link_libraries(sandbox-spawner
  PUBLIC
  pthread
  ${CURL_LIBRARIES}
  ${SSL_LDFLAGS}
  ${LIBCRYPTO_LDFLAGS}
  ${Boost_LIBRARIES}
  )
target_compile_options(sandbox-spawner PRIVATE ${SLATE_SERVER_COMPILE_OPTIONS} )
install(TARGETS sandbox-spawner RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
# TODO: uninstall

add_custom_target(rpm
  # FOO=BAR is a sacrificial dummy variable to absorb the extra 
  # quotes that cmake erroneously puts on the first variable
  COMMAND FOO=BAR CMAKE_BINARY_DIR=${CMAKE_BINARY_DIR} 
    CMAKE_SOURCE_DIR=${CMAKE_SOURCE_DIR} 
    ${CMAKE_SOURCE_DIR}/resources/build_rpm.sh
  DEPENDS ${SERVICE_SOURCES})
