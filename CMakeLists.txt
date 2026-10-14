cmake_minimum_required(VERSION 3.22)

file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" DVLCE_CAPSULE_VERSION LIMIT_COUNT 1)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS VERSION)
project(DvlceCapsule VERSION ${DVLCE_CAPSULE_VERSION} LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "")
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

option(DVLCE_CAPSULE_BUILD_PLUGIN "Build the JUCE plugin (fetches JUCE)" ON)

add_library(dvlce_capsule_core STATIC core/src/capsule.cpp)
target_include_directories(dvlce_capsule_core PUBLIC core/include)
set_target_properties(dvlce_capsule_core PROPERTIES POSITION_INDEPENDENT_CODE ON)
if(MSVC)
    target_compile_options(dvlce_capsule_core PRIVATE /W4 /fp:precise /bigobj)
else()
    target_compile_options(dvlce_capsule_core PRIVATE -Wall -Wextra -ffp-contract=off)
endif()

add_executable(dvlce_capsule_core_tests core/tests/test_capsule.cpp)
target_link_libraries(dvlce_capsule_core_tests PRIVATE dvlce_capsule_core)

if(DVLCE_CAPSULE_BUILD_PLUGIN)
    add_subdirectory(plugin)
endif()
