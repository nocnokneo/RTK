#=========================================================
CMAKE_MINIMUM_REQUIRED(VERSION 2.8.3 FATAL_ERROR)
if(WIN32 OR NOT EXISTS /dev/urandom)
  CMAKE_MINIMUM_REQUIRED(VERSION 2.8.5)
endif()

#=========================================================
#RTK = Reconstruction toolkit
PROJECT(RTK)
#=========================================================

#=========================================================
#Support for the CTest dashboard testing system
INCLUDE(CTest)
INCLUDE(cmake/RTKExternalData.cmake)
#=========================================================


## Tell CMake to be quiet
CMAKE_POLICY (SET CMP0003 NEW)

## Default to release
IF (NOT CMAKE_BUILD_TYPE)
  SET (CMAKE_BUILD_TYPE Release)
ENDIF (NOT CMAKE_BUILD_TYPE)

IF(BUILD_TESTING)
    ENABLE_TESTING()
ENDIF(BUILD_TESTING)
#=========================================================

#=========================================================
SET(RTK_VERSION_MAJOR "0")
SET(RTK_VERSION_MINOR "1")
SET(RTK_VERSION_PATCH "0")

#=========================================================

#=========================================================
# Help function to debug CMake
MACRO (DD in)
  MESSAGE(${in}=[${${in}}])
ENDMACRO(DD)
#=========================================================

#=========================================================
# Output directories.
IF(NOT LIBRARY_OUTPUT_PATH)
  SET (LIBRARY_OUTPUT_PATH ${RTK_BINARY_DIR}/bin CACHE INTERNAL "Single output directory for building all libraries.")
ENDIF(NOT LIBRARY_OUTPUT_PATH)
IF(NOT EXECUTABLE_OUTPUT_PATH)
  SET (EXECUTABLE_OUTPUT_PATH ${RTK_BINARY_DIR}/bin CACHE INTERNAL "Single output directory for building all executables.")
ENDIF(NOT EXECUTABLE_OUTPUT_PATH)
MARK_AS_ADVANCED(LIBRARY_OUTPUT_PATH EXECUTABLE_OUTPUT_PATH)

SET(RTK_LIBRARY_PATH "${LIBRARY_OUTPUT_PATH}")
SET(RTK_EXECUTABLE_PATH "${EXECUTABLE_OUTPUT_PATH}")
SET(CXX_TEST_PATH ${EXECUTABLE_OUTPUT_PATH})

#=========================================================
# Remove some MS Visual c++ flags
IF(MSVC)
  ADD_DEFINITIONS(-D_CRT_SECURE_NO_WARNINGS -D_CRT_NONSTDC_NO_DEPRECATE -D_SCL_SECURE_NO_WARNINGS)
ENDIF(MSVC)

# Flag required for shared libraries
IF ("${CMAKE_SYSTEM_PROCESSOR}" STREQUAL "x86_64")
  SET (CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fPIC")
  SET (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fPIC")
ENDIF ("${CMAKE_SYSTEM_PROCESSOR}" STREQUAL "x86_64")
#=========================================================

#=========================================================
# Find ITK (required)
FIND_PACKAGE(ITK)
IF(ITK_FOUND)
  INCLUDE(${ITK_USE_FILE})
  if(${ITK_VERSION_MAJOR} EQUAL 3)
    # Check that the ITK_USE_REVIEW is ON
    if(NOT ITK_USE_REVIEW)
      message(FATAL_ERROR "Please set ITK_USE_REVIEW to ON when configuring ITK before using RTK")
    endif(NOT ITK_USE_REVIEW)
  endif(${ITK_VERSION_MAJOR} EQUAL 3)
ELSE(ITK_FOUND)
  MESSAGE(FATAL_ERROR "Cannot build without ITK. Set ITK_DIR.")
ENDIF(ITK_FOUND)

# GPU optional dependencies
SET(CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/cmake" ${CMAKE_MODULE_PATH})

FIND_PACKAGE(OpenCL)
IF(OPENCL_LIBRARIES AND OPENCL_INCLUDE_DIRS)
  INCLUDE_DIRECTORIES(${OPENCL_INCLUDE_DIRS})
  SET(OPENCL_FOUND TRUE)
ELSE(OPENCL_LIBRARIES AND OPENCL_INCLUDE_DIRS)
  SET(OPENCL_FOUND FALSE)
ENDIF(OPENCL_LIBRARIES AND OPENCL_INCLUDE_DIRS)

FIND_PACKAGE(CUDA_wrap)
IF(CUDA_FOUND)
  INCLUDE_DIRECTORIES(${CUDA_INCLUDE_DIRS})
ENDIF(CUDA_FOUND)
#=========================================================

#=========================================================
# Remove some Intel compiler warnings
IF("${CMAKE_CXX_COMPILER_ID}" STREQUAL "Intel")
  STRING(REPLACE "-Wno-unused-parameter" "" CMAKE_C_FLAGS "${CMAKE_C_FLAGS}")
  IF(WIN32)
    SET (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Qwd1268")
  ELSE(WIN32)
    SET (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -wd1268")
  ENDIF(WIN32)
ENDIF("${CMAKE_CXX_COMPILER_ID}" STREQUAL "Intel")
#=========================================================

#=========================================================
# Shared libraries option
OPTION(BUILD_SHARED_LIBS "Build RTK with shared libraries." ${ITK_BUILD_SHARED})
SET(RTK_BUILD_SHARED_LIBS ${BUILD_SHARED_LIBS})
#=========================================================

#=========================================================
# RTK uses KWStyle for checking the coding style
INCLUDE(${RTK_SOURCE_DIR}/cmake/KWStyle/KWStyle.cmake)
#=========================================================


#=========================================================
# If choose to build documentation, then search for Doxygen executables.
OPTION(BUILD_DOXYGEN "Build Doxygen Documentation" OFF)
IF(BUILD_DOXYGEN)
  ADD_SUBDIRECTORY(documentation/Doxygen)
ENDIF(BUILD_DOXYGEN)
#=========================================================

#=========================================================
# Installation variables
if(NOT RTK_INSTALL_RUNTIME_DIR)
  set(RTK_INSTALL_RUNTIME_DIR bin)
endif()
if(NOT RTK_INSTALL_LIB_DIR)
  set(RTK_INSTALL_LIB_DIR lib)
endif()
if(NOT RTK_INSTALL_ARCHIVE_DIR)
  set(RTK_INSTALL_ARCHIVE_DIR lib)
endif()
if(NOT RTK_INSTALL_INCLUDE_DIR)
  set(RTK_INSTALL_INCLUDE_DIR include/RTK-${RTK_VERSION_MAJOR}.${RTK_VERSION_MINOR})
endif()
if(NOT RTK_INSTALL_PACKAGE_DIR)
  set(RTK_INSTALL_PACKAGE_DIR "lib/cmake/RTK-${RTK_VERSION_MAJOR}.${RTK_VERSION_MINOR}")
endif()
#=========================================================

LIST(APPEND RTK_INCLUDE_DIRS "${PROJECT_SOURCE_DIR}/code")
IF(CUDA_FOUND)
  LIST(APPEND RTK_INCLUDE_DIRS "${PROJECT_SOURCE_DIR}/utilities/ITKCudaCommon/include/")
ENDIF(CUDA_FOUND)
LIST(APPEND RTK_INCLUDE_DIRS "${PROJECT_BINARY_DIR}")
INCLUDE_DIRECTORIES(${RTK_INCLUDE_DIRS})

IF(ITK_VERSION_MAJOR LESS "4")
  GET_DIRECTORY_PROPERTY(dirInc INCLUDE_DIRECTORIES)
  FIND_FILE(itkConfigurePath itkConfigure.h ${dirInc}) 
  FILE(STRINGS ${itkConfigurePath} USE_FFTWF REGEX "^#define USE_FFTWF$")
  FILE(STRINGS ${itkConfigurePath} USE_FFTWD REGEX "^#define USE_FFTWD$")
  IF(USE_FFTWF OR USE_FFTWD)
    ADD_SUBDIRECTORY(utilities/itkfftw)
    LIST(APPEND RTK_INCLUDE_DIRS "${PROJECT_SOURCE_DIR}/utilities/itkfftw")
    INCLUDE_DIRECTORIES(BEFORE utilities/itkfftw)
  ENDIF()
  INCLUDE_DIRECTORIES(BEFORE utilities/itkcrop)
ENDIF()

IF(CUDA_FOUND)
  ADD_SUBDIRECTORY(utilities/ITKCudaCommon)
  INCLUDE_DIRECTORIES(utilities/ITKCudaCommon/include)
ENDIF(CUDA_FOUND)

ADD_SUBDIRECTORY(code)

#=========================================================
# Generate RTKConfig.cmake for the build tree.
SET (RTK_USE_FILE "${RTK_SOURCE_DIR}/cmake/UseRTK.cmake")
SET (RTK_LIBRARY_DIRS ${RTK_LIBRARY_PATH})
CONFIGURE_FILE (cmake/RTKConfig.cmake.in RTKConfig.cmake @ONLY)
#=========================================================

option(BUILD_APPLICATIONS "Build RTK applications" ON)
IF(BUILD_APPLICATIONS)
  ADD_SUBDIRECTORY(applications)
ENDIF(BUILD_APPLICATIONS)

IF(BUILD_TESTING)
  ADD_SUBDIRECTORY(testing)
ENDIF(BUILD_TESTING)

option(BUILD_EXAMPLES "Build RTK examples" ON)
IF(BUILD_EXAMPLES)
  ADD_SUBDIRECTORY(examples)
ENDIF(BUILD_EXAMPLES)

ExternalData_Add_Target(rtkData)
