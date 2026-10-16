# Forge Tetris
#
# A game, built on the engine, not part of it.
#
# ForgeTetris links exactly what the runtime links - ForgeApp for the window,
# the renderer and the application shell - and supplies the game itself. The
# engine has no knowledge of tetrominoes: it does not serialise a Tetris
# component, does not carry the rules, and does not branch on what is running.
# Anything that had to know the game existed in order to start it would mean the
# coupling was never really broken.
#
# The rules (TetrisManager) are deliberately free of both the engine and GLFW:
# input arrives as a Tetris::Action, which is what lets the test target below
# exercise the whole game with no window and no graphics device.

add_executable(ForgeTetris)

target_include_directories(ForgeTetris
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}
)

target_sources(ForgeTetris
    PRIVATE
        src/Client.cpp
        src/EntryPoint.h
        src/TetrisApp.h
        src/TetrisApp.cpp
        src/TetrisBoardView.h
        src/TetrisBoardView.cpp
        src/TetrisComponents.h
        src/TetrisInputLayer.h
        src/TetrisInputLayer.cpp
        src/TetrisManager.h
        src/TetrisManager.cpp
        src/TetrisTypes.h
)

target_link_libraries(ForgeTetris
    PRIVATE
        ForgeApp
        nvrhi
        forge_vk_dispatch
)

set_target_properties(ForgeTetris PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(ForgeTetris PRIVATE -Werror)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "MSVC")
    target_compile_options(ForgeTetris PRIVATE /WX)
endif()

# The game reads engine assets (the block model) from its working directory, so
# the engine's resources are staged next to the binary. The game has no assets of
# its own yet - it draws a block out of resources/models/cube.glb and builds its
# scene in code - so there is no per-game resource tree to stage.
#
# If this game ever needs one, add it next to this line rather than reaching into
# the engine's tree:
#
#     file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/resources DESTINATION ${CMAKE_RUNTIME_OUTPUT_DIRECTORY})
file(COPY ${CMAKE_SOURCE_DIR}/resources DESTINATION ${CMAKE_RUNTIME_OUTPUT_DIRECTORY})

install(TARGETS ForgeTetris RUNTIME DESTINATION bin)

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
#
# A separate target from ForgeTests on purpose. The game's rules are the game's,
# and engine tests that reach into games/tetris would fail to build the moment
# that game was removed - a test that only exists to check somebody else's
# code is coupling in the other direction.
#
# Links ForgeCore, not ForgeApp: no window, no Vulkan, so this runs headless in
# CI.
add_executable(ForgeTetrisTests)

if(NOT TARGET Catch2::Catch2WithMain)
    add_subdirectory(${CMAKE_SOURCE_DIR}/vendor/catch2 ${CMAKE_BINARY_DIR}/vendor/catch2)
endif()

target_include_directories(ForgeTetrisTests
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}
)

target_sources(ForgeTetrisTests
    PRIVATE
        tests/TetrisTests.cpp
        tests/TetrisBoardViewTests.cpp
        # The rules, the scene-side presenter and the input mapping. Compiled
        # in rather than linked from the game executable: there is no library to
        # link, and the point of the split is which dependencies each file has.
        src/TetrisManager.cpp
        src/TetrisBoardView.cpp
        # Compiled in for MapKey only - the key bindings are pure, and a
        # mapping that quietly did nothing would be invisible from the game.
        src/TetrisInputLayer.cpp
)

target_link_libraries(ForgeTetrisTests
    PRIVATE
        ForgeCore
        Catch2::Catch2WithMain
        glfw
        ${CMAKE_DL_LIBS}
)

target_compile_definitions(ForgeTetrisTests
    PRIVATE
        FG_NO_VULKAN_RUNTIME
        CATCH_CONFIG_NO_PARALLEL_EXECUTION
)

set_target_properties(ForgeTetrisTests PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(ForgeTetrisTests PRIVATE -Werror)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "MSVC")
    target_compile_options(ForgeTetrisTests PRIVATE /WX)
endif()

add_test(NAME ForgeTetrisTests COMMAND ForgeTetrisTests)
set_tests_properties(ForgeTetrisTests PROPERTIES
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
)