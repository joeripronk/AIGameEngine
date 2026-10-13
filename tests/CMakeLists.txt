# Forge Tests
add_executable(ForgeTests)

target_include_directories(ForgeTests
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/vendor/catch2/src
)

target_sources(ForgeTests
    PRIVATE
        src/CoreTests.cpp
        src/SceneTests.cpp
        src/RendererTests.cpp
        src/PhysicsTests.cpp
        src/ScriptingTests.cpp
)

target_link_libraries(ForgeTests
    PRIVATE
        Forge
        Catch2::Catch2WithMain
        ${CMAKE_DL_LIBS}
)

# Set properties
set_target_properties(ForgeTests PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
)

# Add test executable
enable_testing()
add_test(NAME ForgeTests COMMAND ForgeTests)
