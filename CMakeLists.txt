cmake_minimum_required(VERSION 3.22)
project(ForgeEngine VERSION 0.1.0 LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

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

# Debug/Release configurations
add_compile_definitions(
    FG_DEBUG
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
