cmake_minimum_required (VERSION 3.8)

set_property(GLOBAL PROPERTY USE_FOLDERS ON)

# Build directory
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY   		${CMAKE_BINARY_DIR}/artifacts)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY   		${CMAKE_BINARY_DIR}/artifacts)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY   		${CMAKE_BINARY_DIR}/artifacts)

# Function because lazy
function(adjust_bin_paths lib)
  set_target_properties(${lib}
		PROPERTIES
		RUNTIME_OUTPUT_DIRECTORY 			${CMAKE_BINARY_DIR}/artifacts
		LIBRARY_OUTPUT_DIRECTORY 			${CMAKE_BINARY_DIR}/artifacts
		ARCHIVE_OUTPUT_DIRECTORY 			${CMAKE_BINARY_DIR}/artifacts
	)
endfunction()

project("SpaceCalibrator")

set(SPACECAL_VERSION "" CACHE STRING "Build stamp; defaults to version.txt")
set(SPACECAL_CHANNEL "dev" CACHE STRING "Build channel: dev, beta or release")
if(NOT SPACECAL_CHANNEL MATCHES "^(dev|beta|release)$")
	message(FATAL_ERROR "SPACECAL_CHANNEL must be dev, beta or release (got '${SPACECAL_CHANNEL}')")
endif()
if(SPACECAL_VERSION STREQUAL "")
	if(EXISTS ${CMAKE_SOURCE_DIR}/version.txt)
		file(READ ${CMAKE_SOURCE_DIR}/version.txt SPACECAL_VERSION)
		string(STRIP "${SPACECAL_VERSION}" SPACECAL_VERSION)
	else()
		set(SPACECAL_VERSION "0.0.0.0-DEV")
	endif()
endif()
if(SPACECAL_VERSION MATCHES "^([0-9]+)[.]([0-9]+)[.]([0-9]+)[.]([0-9]+)")
	set(SPACECAL_VERSION_MAJOR ${CMAKE_MATCH_1})
	set(SPACECAL_VERSION_MINOR ${CMAKE_MATCH_2})
	set(SPACECAL_VERSION_PATCH ${CMAKE_MATCH_3})
	set(SPACECAL_VERSION_BUILD ${CMAKE_MATCH_4})
else()
	set(SPACECAL_VERSION_MAJOR 0)
	set(SPACECAL_VERSION_MINOR 0)
	set(SPACECAL_VERSION_PATCH 0)
	set(SPACECAL_VERSION_BUILD 0)
endif()
message(STATUS "SpaceCalibrator ${SPACECAL_VERSION} (${SPACECAL_CHANNEL})")
configure_file(src/common/Version.h.in ${CMAKE_BINARY_DIR}/generated/Version.h @ONLY)
configure_file(src/overlay/Version.rc.in ${CMAKE_BINARY_DIR}/generated/Version.rc @ONLY)
option(SPACECAL_BUILD_TESTS "Build the test executables" ON)

add_library(spacecal_warnings INTERFACE)
if(MSVC)
	set(SPACECAL_MSVC_FLAGS
		/we4715 /we4828
		/we4473 /we4474 /we4475 /we4476 /we4477 /we4478
		/we4775 /we4776 /we4777 /we4778
		/we4002 /we4003
		/MP /utf-8 /GF)
	target_compile_options(spacecal_warnings INTERFACE "$<$<COMPILE_LANGUAGE:C,CXX>:${SPACECAL_MSVC_FLAGS}>")
endif()

# Include project
add_subdirectory ("src")

# Libs
add_subdirectory ("lib")

if(SPACECAL_BUILD_TESTS)
	enable_testing()
	add_subdirectory ("tests")
endif()
