# Building for Android

The engine runs on a phone. The game's package is built from the engine's
own CMake with the NDK's toolchain file, and the package around it - the
manifest, the assets, the native library - is built by Gradle.

## What is built

| Target | What it is |
|--------|------------|
| `ForgeTetris` | The game, as `libForgeTetris.so` - the package's native library |
| `ForgeCore`, `vulkan`, `ForgeApp` | The engine, linked into the library above |

The editor, the runtime and the tests are **not** built for a phone: they
are desktop programs, and the game's test target is a desktop program too.

## What a phone has, and what the engine does about it

**There is no window to create.** A desktop window is GLFW's; a phone's
window is the system's own surface, which the system hands over when the
activity is ready. `Forge::AndroidWindow` is the engine's `Window` for it:
it creates nothing, it reports what the system says, and the Vulkan surface
is made from the system's window with the loader's Android surface
extension.

**The input arrives as the system's events.** The system's glue reads them
and hands them over; the engine turns them into the same events a desktop
produces - a key press, a key release, a pointer move, a pointer button.
A finger is the left button.

**The polled input has no window to ask.** On a desktop the queries ask
GLFW; on a phone there is no GLFW, so the platform keeps the state and
reports it (`Forge::Input::SetKeyPressed` and friends), and the queries
answer from that.

**The files are in a package, and a package is not a filesystem.** The
engine reads its files by path - `resources/models/cube.glb` - so the
assets are written out of the package into the app's own directory on the
first run, and the engine runs from there.

**The entry point is the system's.** The system's glue calls
`ANativeActivity_onCreate`, and the app's entry point is `android_main` -
the same place the desktop's `main` is, and the game includes it the same
way.

## Building it

Needs the Android SDK with the NDK, and Gradle.

```bash
# The NDK the package asks for (the package's build.gradle names it, and
# the two have to agree).
sdkmanager "ndk;28.2.13676358"
sdkmanager "cmake;3.22.1"     # the CMake the Gradle build uses

# The package.
tools/build-android.sh            # a release package
tools/build-android.sh --debug    # a debuggable one
```

The package lands in `android/app/build/outputs/apk/`. The debug package is
signed with the debug key and can be installed as it is; the release one is
unsigned and needs a key of your own.

```bash
adb install android/app/build/outputs/apk/debug/app-debug.apk
```

### The native library on its own

```bash
cmake -S . -B build-android \
    -DCMAKE_TOOLCHAIN_FILE=$ANDROID_HOME/ndk/28.2.13676358/build/cmake/android.toolchain.cmake \
    -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-34 -DANDROID_STL=c++_shared
cmake --build build-android --target ForgeTetris
```

## What it needs from a device

**Vulkan 1.3.** The engine asks the loader for 1.3, so the package needs
Android 13 (API 33) or newer - that is what `minSdk` says. A package that
asked for less would run on more phones, and the engine's renderer is
written for 1.3.

**arm64.** The package is built for `arm64-v8a`. The build takes `--abi`
for the others; the engine has no 32-bit answer to give.

## What does not work there

- **The on-screen overlay.** ImGui needs a platform backend, and the one
  that exists is GLFW's. The game runs without its HUD - which is what the
  engine's layer contract says a game does when the backends are not there.
- **The editor.** A phone is not a place to edit a scene.
- **The tests.** They are built and run on a desktop.

## The layout

```
android/                         The package: the manifest, the assets, the Gradle build
  app/build.gradle               The package's build: the ABI, the API level, the assets
  app/src/main/AndroidManifest.xml
src/Platform/Android/            The platform layer: the window, the input, the assets, the entry
src/Platform/Vulkan/PlatformSurface.{h,cpp}   The surface: GLFW's on a desktop, the system's on a phone
```
