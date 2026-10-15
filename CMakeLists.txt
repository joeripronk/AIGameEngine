cmake_minimum_required(VERSION 3.22)
project(ForgeEngine VERSION 0.1.0 LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

enable_testing()

# Repeat static libraries on the link line so inter-archive symbol
# dependencies (nvrhi <-> nvrhi_vk, ForgeCore <-> vulkan) resolve regardless
# of declaration order.
set(CMAKE_LINK_INTERFACE_MULTIPLICITY 3)

# nvrhi_vk references symbols in the nvrhi core library and vice versa; a
# link group lets the linker resolve that mutual dependency in any order.
# The group is opened before the objects and closed after the libraries
# (via CMAKE_CXX_STANDARD_LIBRARIES, which is emitted last).
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang" OR CMAKE_CXX_COMPILER_ID MATCHES "AppleClang")
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -Wl,--start-group")
    string(APPEND CMAKE_CXX_STANDARD_LIBRARIES " -Wl,--end-group")
endif()
# macOS linker needs -Wl,-force_load for static library symbol resolution
if(APPLE)
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -Wl,-force_load")
endif()

# Output directories
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# Platform detection
if(WIN32)
    set(FG_PLATFORM_WINDOWS TRUE)
elseif(APPLE)
    set(FG_PLATFORM_MACOS TRUE)
elseif(UNIX)
    set(FG_PLATFORM_LINUX TRUE)
endif()

# Compiler flags
if(CMAKE_CXX_COMPILER_ID MATCHES "MSVC")
    add_compile_options(/W4)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# Build configuration
#
# Single-config generators (Makefiles, Ninja) need a type up front. Leaving it
# empty used to compile no configuration macro at all, which silently disabled
# assertions in the default local build; defaulting to Debug keeps the
# development loop honest.
get_property(FG_MULTI_CONFIG GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
if(NOT FG_MULTI_CONFIG AND NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
    message(STATUS "CMAKE_BUILD_TYPE was unset; defaulting to Debug")
endif()

# Dist is the shipping configuration. CMake has no built-in Dist, so without
# this its flags are empty and the build gets no optimisation at all - which
# made the shipping binary *larger* than an ordinary Release build. Dist is
# Release's optimisation plus NDEBUG; the behavioural difference (log level) is
# handled by FG_DIST below.
if(FG_MULTI_CONFIG)
    # Multi-config generators (Visual Studio, Xcode) need Dist declared
    # alongside the built-in configurations, or it is rejected outright.
    string(REPLACE ";" ";" CMAKE_CONFIGURATION_TYPES "${CMAKE_CONFIGURATION_TYPES};Dist"
        CACHE STRING "Configurations" FORCE)

    if(MSVC)
        set(CMAKE_CXX_FLAGS_DIST "/O2 /Ob2 /DNDEBUG")
    else()
        # Xcode and other multi-config generators use the GCC/Clang spelling.
        set(CMAKE_CXX_FLAGS_DIST "-O2 -DNDEBUG")
    endif()
else()
    set(CMAKE_CXX_FLAGS_DIST "-O2 -DNDEBUG")
endif()

# FG_DEBUG gates FG_ENABLE_ASSERTS and the debugbreak trap, so it must follow
# the actual configuration. Defining it unconditionally made every build trap
# with SIGTRAP on an assertion failure - including the one that ships, where it
# took the game down with no diagnostic instead of logging and continuing.
add_compile_definitions(
    $<$<CONFIG:Debug>:FG_DEBUG>
    $<$<CONFIG:Release>:FG_RELEASE>
    $<$<CONFIG:Dist>:FG_DIST>
)

# Platform definitions
if(FG_PLATFORM_WINDOWS)
    add_compile_definitions(FG_PLATFORM_WINDOWS)
elseif(FG_PLATFORM_MACOS)
    add_compile_definitions(FG_PLATFORM_MACOS)
elseif(FG_PLATFORM_LINUX)
    add_compile_definitions(FG_PLATFORM_LINUX)
endif()

# Subdirectories
add_subdirectory(src)
add_subdirectory(editor)
add_subdirectory(runtime)
add_subdirectory(tests)

# Install rules
install(TARGETS ForgeCore DESTINATION lib)
install(DIRECTORY src/Forge DESTINATION include/Forge)
