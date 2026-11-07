cmake_minimum_required(VERSION 3.10)
project(home_manager)
set(CMAKE_CXX_STANDARD 17)
if (CMAKE_HOSTS_SYSTEM_NAME STREQUAL "Linux" )
    add_definitions(-DOS_LINUX -DOS_UNIX)
endif()
add_definitions(-DDEBUG)
add_compile_options(-Wno-psabi)
set(CMAKE_BUILD_TYPE Debug)
set(SYSTEM_LIBS pthread dl)
file(GLOB SOURCE_FILES
    "*.h"
    "*.cpp"
)
# For clang
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
file(GLOB_RECURSE ALL_SOURCES ${CMAKE_SOURCE_DIR}/src/*.cpp)

add_custom_target(lint
    COMMAND docker run --rm
            -v ${CMAKE_SOURCE_DIR}:/workspace
            -v ${CMAKE_BINARY_DIR}:/workspace/build
            -w /workspace
            silkeh/clang
            clang-tidy -p /workspace/build
            ${ALL_SOURCES}
            -- -I/workspace/include
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "Running clang-tidy in Docker"
    VERBATIM
)

add_custom_target(clean-cmake
    COMMAND ${CMAKE_COMMAND} -E rm -rf 
        ${CMAKE_BINARY_DIR}/CMakeCache.txt
        ${CMAKE_BINARY_DIR}/CMakeFiles
        ${CMAKE_BINARY_DIR}/cmake_install.cmake
    COMMENT "Cleaning CMake-generated files"
)
# Set paths explicitly if pigpio is installed in a non-standard location
set(PIGPIO_INCLUDE_DIR "/usr/include")
set(PIGPIO_LIBRARY "/usr/lib/libpigpio.so")

# Add include directory
include_directories(${PIGPIO_INCLUDE_DIR})

#find_package(PkgConfig REQUIRED)
#pkg_check_modules(PIGPIO REQUIRED pigpio)

####### JSON
# Typically you don't care so much for a third party library's tests to be
# run from your own project's code.
#set(JSON_BuildTests OFF CACHE INTERNAL "")

# If you only include this third party in PRIVATE source files, you do not
# need to install it when your main project gets installed.
# set(JSON_Install OFF CACHE INTERNAL "")

# Don't use include(nlohmann_json/CMakeLists.txt) since that carries with it
# unintended consequences that will break the build.  It's generally
# discouraged (although not necessarily well documented as such) to use
# include(...) for pulling in other CMake projects anyways.
#add_subdirectory(../../3rdParty/json ../../3rdParty/json)
#...
#add_library(home_manager ...)
#...
#target_link_libraries(home_manager PRIVATE nlohmann_json::nlohmann_json)

#######
#${CMAKE_CURRENT_SOURCE_DIR}
set(json_ROOT "../3rdParty/json")
set(json_INCLUDES "${json_ROOT}/include")
include_directories(SYSTEM ${json_INCLUDES})
include_directories("./include")
include_directories("../3rdParty")
#######
find_library(THREAD NAMES pthread)
find_library(CRYPT NAMES crypt)
find_library(SHM_OPEN NAMES rt)
find_package(CURL REQUIRED)

#include_directories(SYSTEM ../../3rdParty/json)
add_executable(home_manager ${SOURCE_FILES})
target_link_libraries(home_manager ${CMAKE_DL_LIBS})
target_link_libraries(home_manager ${THREAD})
target_link_libraries(home_manager ${CRYPT} ${SHM_OPEN} ${PIGPIO_LIBRARIES} -lpigpio -lgpiod )
target_link_libraries(home_manager CURL::libcurl)

