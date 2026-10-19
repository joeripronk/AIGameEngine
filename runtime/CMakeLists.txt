# Forge Runtime
#
# This is the shipping executable: the game, with no editor. The distribution
# *configuration* is what makes it a distribution build, not a separate target:
#
#     cmake -S . -B build-dist -DCMAKE_BUILD_TYPE=Dist
#     cmake --build build-dist
#     tools/deploy.sh Dist
#
# The Dist configuration compiles out assertions and the debugbreak trap, and
# raises the log level to warn. All of that lives in ForgeCore, so a separate
# executable target cannot switch it on - a static library keeps the
# configuration it was built with, and a target cannot reconfigure the world.
#
# An earlier ForgeRuntimeDist target tried to do this per-executable. It had
# exactly the same sources and libraries as ForgeRuntime, and the same
# OUTPUT_NAME, so both wrote to bin/ForgeRuntime: whichever linked last silently
# won, and one target's artefact satisfied the other's up-to-date check. It was
# removed rather than renamed, because it added no capability.
add_executable(ForgeRuntime)

target_include_directories(ForgeRuntime
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/runtime
        ${CMAKE_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/Platform/Vulkan
)

target_sources(ForgeRuntime
    PRIVATE
        src/Client.cpp
        src/EntryPoint.h
        src/RuntimeApp.h
        src/RuntimeApp.cpp
)

target_link_libraries(ForgeRuntime
    PRIVATE
        ForgeApp
)

set_target_properties(ForgeRuntime PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
)

# Enable -Werror for runtime code
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(ForgeRuntime PRIVATE -Werror)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "MSVC")
    target_compile_options(ForgeRuntime PRIVATE /WX)
endif()

# Copy resources
file(COPY ${CMAKE_SOURCE_DIR}/resources DESTINATION ${CMAKE_RUNTIME_OUTPUT_DIRECTORY})

# Install rules
install(TARGETS ForgeRuntime RUNTIME DESTINATION bin)