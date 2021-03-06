# Licensed to the Apache Software Foundation (ASF) under one
#
# or more contributor license agreements.  See the NOTICE file
# distributed with this work for additional information
# regarding copyright ownership.  The ASF licenses this file
# to you under the Apache License, Version 2.0 (the
# "License"); you may not use this file except in compliance
# with the License.  You may obtain a copy of the License at
#
#   http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing,
# software distributed under the License is distributed on an
# "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
# KIND, either express or implied.  See the License for the
# specific language governing permissions and limitations
# under the License.
#

cmake_minimum_required(VERSION 3.0)
project(nifi-minifi-cpp)
set(PROJECT_NAME "nifi-minifi-cpp")
set(PROJECT_VERSION_MAJOR 0)
set(PROJECT_VERSION_MINOR 4)
set(PROJECT_VERSION_PATCH 0)
option(SKIP_TESTS "Skips building all tests." OFF)
option(PORTABLE "Instructs the compiler to remove architecture specific optimizations" ON)

include(FeatureSummary)

# Use ccache if present
find_program(CCACHE_FOUND ccache)
if(CCACHE_FOUND)
    set_property(GLOBAL PROPERTY RULE_LAUNCH_COMPILE ccache)
    set_property(GLOBAL PROPERTY RULE_LAUNCH_LINK ccache)
    message("-- Found ccache: ${CCACHE_FOUND}")
endif(CCACHE_FOUND)

#### Establish Project Configuration ####
# Enable usage of the VERSION specifier
include(CheckCXXCompilerFlag)
CHECK_CXX_COMPILER_FLAG("-std=c++11 " COMPILER_SUPPORTS_CXX11)
CHECK_CXX_COMPILER_FLAG("-std=c++0x " COMPILER_SUPPORTS_CXX0X)
SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ")
SET(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS}")
if(COMPILER_SUPPORTS_CXX11)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11")
elseif(COMPILER_SUPPORTS_CXX0X)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++0x")
else()
    message(STATUS "The compiler ${CMAKE_CXX_COMPILER} has no C++11 support. Please use a different C++ compiler.")
endif()

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall")

if (NOT PORTABLE)
  if(MSVC)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /arch:AVX2")
  else()
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -march=native")
  endif()
endif()

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Search for threads
find_package(Threads REQUIRED)

# Set the right openssl root path
if (${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
    set(OPENSSL_ROOT_DIR "/usr/local/opt/openssl/")
else()
    set(OPENSSL_ROOT_DIR "/usr/lib/x86_64-linux-gnu")
endif()

# Include OpenSSL
find_package (OpenSSL REQUIRED)
if (OPENSSL_FOUND)
    include_directories(${OPENSSL_INCLUDE_DIR})
else ()
    message( FATAL_ERROR "OpenSSL was not found. Please install OpenSSL" )
endif (OPENSSL_FOUND)

# Provide custom modules for the project
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

SET(TEST_DIR ${CMAKE_SOURCE_DIR}/libminifi/test)

include(Extensions)

find_package(UUID REQUIRED)

file(GLOB SPD_SOURCES "thirdparty/spdlog-20170710/include/spdlog/*")

include(ExternalProject)

set(CIVETWEB_ENABLE_SSL_DYNAMIC_LOADING OFF CACHE BOOL "Disable dynamic SSL library loading")
set(CIVETWEB_ENABLE_CXX ON CACHE BOOL "Enable civet C++ library")
SET(WITH_TOOLS OFF CACHE BOOL "Do not build RocksDB tools")
SET(WITH_TESTS OFF CACHE BOOL "Build RocksDB library (not repo) tests")
set(CIVET_THIRDPARTY_ROOT "${CMAKE_SOURCE_DIR}/thirdparty/civetweb-1.10/" CACHE STRING "Path to CivetWeb root")
set(CIVET_BINARY_ROOT "${CMAKE_BINARY_DIR}/thirdparty/civetweb-1.10/" CACHE STRING "Path to CivetWeb binary output")
set(ROCKSDB_THIRDPARTY_ROOT "${CMAKE_SOURCE_DIR}/thirdparty/rocksdb/" CACHE STRING "Path to RocksDB root")
add_subdirectory(thirdparty/yaml-cpp-yaml-cpp-20171024)

include_directories(thirdparty/concurrentqueue)
include_directories(thirdparty/yaml-cpp-yaml-cpp-20171024/include)
include_directories(thirdparty/rapidjson-1.1.0/include)

## Expression language extensions
option(DISABLE_EXPRESSION_LANGUAGE "Disables the scripting extensions." OFF)
if (DISABLE_EXPRESSION_LANGUAGE)
    # Build expression language NoOp implementation, if necessary
    include_directories("extensions/expression-language/noop")
    add_subdirectory(extensions/expression-language/noop)
else()
    include_directories("extensions/expression-language/impl")
    createExtension(EXPRESSION-LANGUAGE-EXTENSIONS "EXPRESSION LANGUAGE EXTENSIONS" "This enables NiFi expression language" "extensions/expression-language" "${TEST_DIR}/expression-language-tests")
endif()


add_subdirectory(libminifi)

#### EXTENSION
option(DISABLE_CURL "Disables libCurl Properties." OFF)
if (NOT DISABLE_CURL AND NOT DISABLE_CIVET)
	createExtension(HTTP-CURL "HTTP CURL" "This enables RESTProtocol, InvokeHTTP, and the HTTPClient for Site to Site" "extensions/http-curl" "extensions/http-curl/tests/")
endif()

option(DISABLE_CIVET "Disables CivetWeb components." OFF)
if (NOT DISABLE_CIVET)
createExtension(CIVETWEB CIVETWEB "This enables ListenHTTP" "extensions/civetweb")
endif()

if (NOT DISABLE_CURL AND NOT DISABLE_CONTROLLER)
	add_subdirectory(thirdparty/cxxopts)
	add_subdirectory(controller)
endif()

## Add the rocks DB extension
if (NOT ROCKSDB_FOUND OR BUILD_ROCKSDB)
	set(BUILD_RD "TRUE")
endif()

option(DISABLE_ROCKSDB "Disables the RocksDB extension." OFF)
if (NOT DISABLE_ROCKSDB)
	createExtension(ROCKSDB-REPOS "ROCKSDB REPOS" "This Enables persistent provenance, flowfile, and content repositories using RocksDB" "extensions/rocksdb-repos" "${TEST_DIR}/rocksdb-tests" BUILD_RD "${ROCKSDB_THIRDPARTY_ROOT}")
endif()

## Create LibArchive Extension
if (NOT LibArchive_FOUND OR BUILD_LIBARCHIVE)
	set(BUILD_TP "TRUE")
endif()
option(DISABLE_LIBARCHIVE "Disables the lib archive extensions." OFF)
if (NOT DISABLE_LIBARCHIVE)
	createExtension(ARCHIVE-EXTENSIONS "ARCHIVE EXTENSIONS" "This Enables libarchive functionality including MergeContent, CompressContent, (Un)FocusArchiveEntry and ManipulateArchive." "extensions/libarchive" "${TEST_DIR}/archive-tests" BUILD_TP "thirdparty/libarchive-3.3.2")
endif()

option(ENABLE_GPS "Enables the GPS extension." OFF)
if (ENABLE_ALL OR ENABLE_GPS)
	createExtension(GPS-EXTENSION "GPS EXTENSIONS" "Enables LibGPS Functionality and the GetGPS processor." "extensions/gps" "${TEST_DIR}/gps-tests")
endif()

## Create MQTT Extension
option(ENABLE_MQTT "Enables the mqtt extension." OFF)
if(ENABLE_ALL OR ENABLE_MQTT)
        createExtension(MQTT-EXTENSIONS "MQTT EXTENSIONS" "This Enables MQTT functionality including PublishMQTT/ConsumeMQTT" "extensions/mqtt" "${TEST_DIR}/mqtt-tests" "TRUE" "thirdparty/paho.mqtt.c")
endif()

option(ENABLE_PCAP "Enables the PCAP extension." OFF)
if(ENABLE_ALL OR ENABLE_PCAP)
	createExtension(PCAP-EXTENSION "PCAP EXTENSIONS" "Enables libPCAP Functionality and the PacketCapture processor." "extensions/pcap" "${TEST_DIR}/pcap-tests")
endif()

## Create LibRdKafka Extension
option(ENABLE_LIBRDKAFKA "Enables the librdkafka extension." OFF)
if (ENABLE_ALL OR ENABLE_LIBRDKAFKA)
	createExtension(RDKAFKA-EXTENSIONS "RDKAFKA EXTENSIONS" "This Enables librdkafka functionality including PublishKafka" "extensions/librdkafka" "${TEST_DIR}/kafka-tests" "TRUE" "thirdparty/librdkafka-0.11.1")
endif()

## Scripting extensions
option(DISABLE_SCRIPTING "Disables the scripting extensions." OFF)
if (NOT DISABLE_SCRIPTING)
    createExtension(SCRIPTING-EXTENSIONS "SCRIPTING EXTENSIONS" "This enables scripting" "extensions/script" "${TEST_DIR}/script-tests")
endif()


## USB camera extensions
option(ENABLE_USB_CAMERA "Enables USB camera support." OFF)
if (ENABLE_ALL OR ENABLE_USB_CAMERA)
    createExtension(USB-CAMERA-EXTENSIONS "USB CAMERA EXTENSIONS" "This enables USB camera support" "extensions/usb-camera" "${TEST_DIR}/usb-camera-tests" "TRUE" "thirdparty/libuvc-0.0.6")
endif()

## TensorFlow extensions
## Disabled by default because TF can be complex/environment-specific to build
option(ENABLE_TENSORFLOW "Disables the TensorFlow extensions." OFF)
if (ENABLE_TENSORFLOW)
    createExtension(TENSORFLOW-EXTENSIONS "TENSORFLOW EXTENSIONS" "This enables TensorFlow support" "extensions/tensorflow" "${TEST_DIR}/tensorflow-tests")
endif()

## Bustache/template extensions
option(ENABLE_BUSTACHE "Enables Bustache (ApplyTemplate) support." OFF)
if (ENABLE_BUSTACHE)
    createExtension(BUSTACHE-EXTENSIONS "BUSTACHE EXTENSIONS" "This enables bustache functionality including ApplyTemplate." "extensions/bustache" "${TEST_DIR}/bustache-tests" "TRUE" "thirdparty/bustache")
endif()

## NOW WE CAN ADD LIBRARIES AND EXTENSIONS TO MAIN

add_subdirectory(main)
if (NOT DISABLE_CURL)
  add_subdirectory(LibExample)
endif()


# Generate source assembly
set(ASSEMBLY_BASE_NAME "${CMAKE_PROJECT_NAME}-${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}.${PROJECT_VERSION_PATCH}")
set(CPACK_SOURCE_GENERATOR "TGZ")
set(CPACK_SOURCE_PACKAGE_FILE_NAME "${ASSEMBLY_BASE_NAME}-source")
set(CPACK_SOURCE_IGNORE_FILES "/docs/generated/;${CMAKE_SOURCE_DIR}/build/;~$;${CPACK_SOURCE_IGNORE_FILES};${CMAKE_SOURCE_DIR}/.git/;${CMAKE_SOURCE_DIR}/.idea/;${CMAKE_SOURCE_DIR}/cmake-build-debug/;${CMAKE_SOURCE_DIR}/extensions/expression-language/Scanner.h;${CMAKE_SOURCE_DIR}/extensions/expression-language/Scanner.cpp;${CMAKE_SOURCE_DIR}/extensions/expression-language/Parser.cpp;${CMAKE_SOURCE_DIR}/extensions/expression-language/Parser.hpp;${CMAKE_SOURCE_DIR}/extensions/expression-language/scanner.hh;${CMAKE_SOURCE_DIR}/extensions/expression-language/position.hh;${CMAKE_SOURCE_DIR}/extensions/expression-language/location.hh;${CMAKE_SOURCE_DIR}/extensions/expression-language/stack.hh")

# Generate binary assembly
install(FILES conf/minifi.properties conf/minifi-log.properties conf/minifi-uid.properties conf/config.yml
        DESTINATION conf
        COMPONENT bin)

install(PROGRAMS bin/minifi.sh
        DESTINATION bin
        COMPONENT bin)

install(FILES LICENSE README.md NOTICE
        DESTINATION .
        COMPONENT bin)

set(CPACK_GENERATOR "TGZ")
set(CPACK_COMPONENT_INCLUDE_TOPLEVEL_DIRECTORY 1)
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Apache NiFi MiNiFi C++ version ${VERSION}")
set(CPACK_PACKAGE_VENDOR "Apache NiFi")
set(CPACK_PACKAGE_DESCRIPTION_FILE "${CMAKE_CURRENT_SOURCE_DIR}/README.md")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")
set(CPACK_PACKAGE_FILE_NAME "${ASSEMBLY_BASE_NAME}")
set(CPACK_BINARY_TGZ, "ON")
set(CPACK_ARCHIVE_COMPONENT_INSTALL ON)
set(CPACK_RDKAFKA_COMPONENT_INSTALL ON)
set(CPACK_MQTT_COMPONENT_INSTALL ON)
set(CPACK_COMPONENTS_ALL bin)


### include modules

include(CPack)

if (NOT SKIP_TESTS)
	include(BuildTests)
endif()

include(BuildDocs)

include(DockerConfig)

# Create a custom build target that will run the linter.
add_custom_target(
    linter
    COMMAND ${CMAKE_SOURCE_DIR}/thirdparty/google-styleguide/run_linter.sh ${CMAKE_SOURCE_DIR}/libminifi/include/ ${CMAKE_SOURCE_DIR}/libminifi/src/
    COMMAND ${CMAKE_SOURCE_DIR}/thirdparty/google-styleguide/run_linter.sh ${CMAKE_SOURCE_DIR}/libminifi/include/ ${CMAKE_SOURCE_DIR}/libminifi/test/ )

# Custom target to download and run Apache Release Audit Tool (RAT)
add_custom_target(
        apache-rat
        ${CMAKE_COMMAND} -P ${CMAKE_SOURCE_DIR}/cmake/RunApacheRAT.cmake
        COMMAND java -jar ${CMAKE_SOURCE_DIR}/thirdparty/apache-rat/apache-rat-0.12/apache-rat-0.12.jar -E ${CMAKE_SOURCE_DIR}/thirdparty/apache-rat/.rat-excludes -d ${CMAKE_SOURCE_DIR} | grep -B 1 -A 15 Summary )

feature_summary(WHAT ALL FILENAME ${CMAKE_BINARY_DIR}/all.log)
