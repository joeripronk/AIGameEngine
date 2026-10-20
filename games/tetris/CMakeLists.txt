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

# On a phone the game is the package's native library: the system loads it
# and calls its entry point, which is the platform's (the system's glue hands
# the app over and the app runs).
if(FG_ANDROID)
    add_library(ForgeTetris SHARED)

    # The NDK's own glue, compiled into the package's library.
    #
    # The system looks the glue's entry point up by name in the library it
    # loads, and a static archive's member is only linked when something
    # asks for it - so the glue has to be in the library itself, not in an
    # archive beside it.
    target_sources(ForgeTetris PRIVATE ${FORGE_ANDROID_GLUE_DIR}/android_native_app_glue.c)
    target_include_directories(ForgeTetris PRIVATE ${FORGE_ANDROID_GLUE_DIR})

    # The glue is the NDK's own code, and this build treats warnings as
    # errors: the two warnings it draws are about the NDK's style (a
    # function without a prototype, parameters it does not use), not about
    # this engine's. They are silenced for that file alone - a flag on the
    # target would silence them everywhere, including where they would be
    # worth hearing.
    set_source_files_properties(${FORGE_ANDROID_GLUE_DIR}/android_native_app_glue.c
        PROPERTIES COMPILE_OPTIONS "-Wno-strict-prototypes;-Wno-unused-parameter")
else()
    add_executable(ForgeTetris)
endif()

target_include_directories(ForgeTetris
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}
)

target_sources(ForgeTetris
    PRIVATE
        src/Client.cpp
        src/EntryPoint.h
        src/TetrisAnimation.h
        src/TetrisApp.h
        src/TetrisApp.cpp
        src/TetrisBoardView.h
        src/TetrisBoardView.cpp
        src/TetrisComponents.h
        src/TetrisGameScript.h
        src/TetrisGameScript.cpp
        src/TetrisInputLayer.h
        src/TetrisInputLayer.cpp
        src/TetrisInputOverlayLayer.h
        src/TetrisLightShow.h
        src/TetrisLightShow.cpp
        src/TetrisManager.h
        src/TetrisManager.cpp
        src/TetrisMusic.h
        src/TetrisMusic.cpp
        src/TetrisMusicTrack.h
        src/TetrisMusicTrack.cpp
        src/TetrisOverlay.h
        src/TetrisOverlay.cpp
        src/TetrisTypes.h
)

target_link_libraries(ForgeTetris
    PRIVATE
        ForgeApp
        imgui
)
if(NOT FG_ANDROID)
    target_link_libraries(ForgeTetris PRIVATE glfw)
endif()

set_target_properties(ForgeTetris PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(ForgeTetris PRIVATE -Werror)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "MSVC")
    target_compile_options(ForgeTetris PRIVATE /utf-8)
    target_link_options(ForgeTetris PRIVATE /FORCE:MULTIPLE)
endif()

# The game reads engine assets (the block model) from its working directory, so
# the engine's resources are staged next to the binary. The game has no assets of
# its own yet - it draws a block out of resources/models/cube.glb and builds its
# scene in code - so there is no per-game resource tree to stage.
#
# Its audio lives in the shared resources/audio tree: the songs in the playlist
# (TetrisMusicTrack.cpp) are authored mp3s, and the one song the game makes for
# itself - resources/audio/tetris_theme.wav - is synthesised from the melody
# table on first run, so it is a generated file and not tracked.
#
# If this game ever needs one, add it next to this line rather than reaching into
# the engine's tree:
#
#     file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/resources DESTINATION ${CMAKE_RUNTIME_OUTPUT_DIRECTORY})
# The resources are staged next to the binary on a desktop. On a phone they
# are the package's assets, and the Gradle build puts them there.
if(NOT FG_ANDROID)
    file(COPY ${CMAKE_SOURCE_DIR}/resources DESTINATION ${CMAKE_RUNTIME_OUTPUT_DIRECTORY})
endif()

if(NOT FG_ANDROID)
    install(TARGETS ForgeTetris RUNTIME DESTINATION bin)
endif()

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
#
# A desktop program: the tests are built and run where the tests are, and a
# phone is not where they are run.
if(NOT FG_ANDROID)

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
        tests/TetrisAnimationTests.cpp
        tests/TetrisBoardViewTests.cpp
        tests/TetrisLightShowTests.cpp
        tests/TetrisMusicTests.cpp
        # The rules, the scene-side presenter and the input mapping. Compiled
        # in rather than linked from the game executable: there is no library to
        # link, and the point of the split is which dependencies each file has.
        src/TetrisManager.cpp
        src/TetrisBoardView.cpp
        # The light show: the lamps over the playfield and the show they put
        # on. The show itself is pure, so it is assertable here.
        src/TetrisLightShow.cpp
        # The game's script: the scene names it, and the scene's runtime
        # runs it, so a test can drive the whole thing headless.
        src/TetrisGameScript.cpp
        # Compiled in for the input bindings - they are pure, and a mapping
        # that quietly did nothing would be invisible from the game.
        src/TetrisInputLayer.cpp
        # The input layer now forwards to the overlay so a paused/reset action
        # refreshes the HUD; tests link ImGui to keep that object resolvable.
        src/TetrisOverlay.cpp
        # The theme: the melody, the synth and the bake are all device
        # independent, so the tune and its loop seam are assertable here.
        src/TetrisMusicTrack.cpp
        src/TetrisMusic.cpp
)

target_link_libraries(ForgeTetrisTests
    PRIVATE
        ForgeCore
        Catch2::Catch2WithMain
        glfw
        imgui
        ${CMAKE_DL_LIBS}
)

target_compile_definitions(ForgeTetrisTests
    PRIVATE
        FG_NO_VULKAN_RUNTIME
        CATCH_CONFIG_NO_PARALLEL_EXECUTION
        # The scene the game ships lives in the repository's resources, and
        # the tests run from the game's own directory.
        FORGE_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)

set_target_properties(ForgeTetrisTests PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(ForgeTetrisTests PRIVATE -Werror)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "MSVC")
    target_compile_options(ForgeTetrisTests PRIVATE /utf-8)
    target_link_options(ForgeTetrisTests PRIVATE /FORCE:MULTIPLE)
endif()

# The scene authoring tool: it builds the scene the game ships with the
# editor's own model, so what it writes is what the editor would write.
if(NOT FG_ANDROID)
add_executable(TetrisSceneAuthor)

target_include_directories(TetrisSceneAuthor
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}
)

target_sources(TetrisSceneAuthor
    PRIVATE
        tools/AuthorScene.cpp
        src/TetrisBoardView.cpp
        src/TetrisGameScript.cpp
        src/TetrisLightShow.cpp
        src/TetrisManager.cpp
)

target_link_libraries(TetrisSceneAuthor
    PRIVATE
        ForgeApp
        ForgeCore
        glfw
)

set_target_properties(TetrisSceneAuthor PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(TetrisSceneAuthor PRIVATE -Werror)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "MSVC")
    target_compile_options(TetrisSceneAuthor PRIVATE /utf-8)
    target_link_options(TetrisSceneAuthor PRIVATE /FORCE:MULTIPLE)
endif()

endif()  # NOT FG_ANDROID

add_test(NAME ForgeTetrisTests COMMAND ForgeTetrisTests)
set_tests_properties(ForgeTetrisTests PROPERTIES
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
)

endif()  # NOT FG_ANDROID

# The resize integration check: the game resizes its own window and the
# frame it captures afterwards has to be the new size. The unit tests cannot
# answer that - it needs a real swapchain and a real presentation - and it
# is the check that would have caught the renderer keeping the size the
# window started at, which is what the game did on Linux.
#
# The script skips gracefully where no window is possible, so it is CI-safe.
add_test(NAME ResizeCheck
    COMMAND bash "${CMAKE_SOURCE_DIR}/tests/integration/resize_check.sh" "$<TARGET_FILE:ForgeTetris>"
)
set_tests_properties(ResizeCheck PROPERTIES
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    TIMEOUT 300
)