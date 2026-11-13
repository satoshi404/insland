# Insland Engine

A compact, cross-platform 3D game engine written in C++17. It targets **Windows
(Direct3D 11)**, **Linux (OpenGL)** and **Android (GLES)** through
[Diligent Engine](https://github.com/DiligentGraphics/DiligentEngine), with
skeletal animation, a procedural world, a raymarched sky, CPU ray casting and an
immediate-mode inspector UI.

Insland is the continuation of an earlier `AetherEngine` prototype: the
modular application layer, logging, memory accounting and project workflow were
brought over, while everything the engine already had (skinning, animation,
the demo/validation flow and the multi-backend renderer) was kept intact.

---

## Features

| Area | What you get |
| --- | --- |
| Rendering | D3D11 on Windows, OpenGL/GLES on Linux/Android; one shared HLSL shader source translated to GLSL at runtime |
| Assets | glTF 2.0 and GLB via tinygltf, PBR materials, textures, vertex colours |
| Draco | `KHR_draco_mesh_compression` decoded transparently (positions, normals, UVs, tangents, joints, weights) |
| Animation | Skeletal skinning with `ozz-animation`, animation playback and bounds recomputation |
| World | Seeded fBm island terrain, water plane, instanced trees, `height_at()` / `normal_at()` sampling |
| Sky | Fullscreen raymarched atmosphere and volumetric clouds |
| Picking | Screen ray construction, AABB / triangle / mesh / height-field intersection |
| UI | Dear ImGui backend with font atlas, scissor and alpha blending |
| Foundation | Category logger, memory accounting, filesystem helpers, asset manager, fixed-step `Application` loop |

---

## Requirements

- **CMake** 4.5+
- **Ninja** (used by `CMakePresets.json` on every platform)
- **C++17** compiler
  - Windows: Visual Studio 2022+ (MSVC)
  - Linux: GCC 11+ or Clang 14+ with X11 development headers
  - Android: NDK r25+ and an `ANativeWindow`-based host activity
- Third-party sources are vendored under `external/`, so a normal build is
  fully offline

---

## Building

### Windows / Direct3D 11 (default)

```powershell
cmake --preset windows-d3d11
cmake --build --preset windows-d3d11
```

The executable is written to `build/InslandGame.exe`. Run it from `build/` so
the bundled assets resolve:

```powershell
cd build
.\InslandGame.exe
```

### Windows / OpenGL (validates the Linux renderer without leaving Windows)

```powershell
cmake --preset windows-opengl
cmake --build --preset windows-opengl
```

### Visual Studio Code

`CMakePresets.json` is tracked and `.vscode/settings.json` enables it, so the
CMake Tools status-bar buttons use the same Ninja generator and binary
directories as the command line:

1. Install the **CMake Tools** and **C/C++** extensions (VS Code offers them
   automatically via `.vscode/extensions.json`).
2. Open the project and pick a configure preset in the status bar:
   `Windows - Direct3D 11 (Ninja, Release)` or `Windows - OpenGL (Ninja, Release)`.
3. Pick the `InslandGame` launch target and press **Build** (`F7`) or **Play**.

#### Running the sample with the Play button

The Play button runs the **launch target**, not necessarily the demo. To run
`InslandSample`:

1. Build once so the target exists (`Build` / `F7`).
2. Click the target name in the CMake status bar (bottom bar, usually
   `InslandGame`).
3. Choose `InslandSample` from the list. You can also run
   **CMake: Set Launch/Debug Target** from the Command Palette.
4. Press the **Play** triangle in the status bar (or run
   **CMake: Run Without Debugging**). VS Code starts
   `build/projects/sample/InslandSample.exe`.

The working directory for a Play run is the build directory. The sample already
searches `../workspace/game/assets`, so the island, model and inspector load
without extra configuration. To pass command-line flags, set
`cmake.debugConfig.args` in `.vscode/settings.json` (for example
`["--frames", "120"]`) or use **CMake: Run Without Debugging** with a launch
configuration.

Do not also set `cmake.generator` / `cmake.buildDirectory`; with presets enabled
those settings are ignored and the presets are authoritative. If a build tree
was previously configured with another generator (for example the Visual Studio
generator), delete it once so CMake can regenerate it:

```powershell
Remove-Item -Recurse -Force build
cmake --preset windows-d3d11
```

### Linux

```bash
sudo apt install build-essential cmake libx11-dev
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --target InslandGame
./build/InslandGame
```

### Android

The Android build uses the NDK CMake toolchain and packages a `NativeActivity`
APK with the SDK build-tools (`aapt2`, `zipalign`, `apksigner`). Gradle is not
required. The script finds the SDK, NDK, JDK and Ninja automatically, downloads
NDK r27c on first use, extracts the bundled assets and signs a debug APK.

```powershell
# arm64-v8a debug APK -> build-android/insland-arm64-v8a.apk
py scripts/build_android.py

# other ABIs / explicit tools
py scripts/build_android.py --abi x86_64
py scripts/build_android.py --ndk "C:/Android/Sdk/ndk/27.3.13750724"
py scripts/build_android.py --output dist/insland.apk

# install on a connected device or emulator
adb install -r build-android/insland-arm64-v8a.apk
```

Requirements:

- Android SDK with a platform (`android.jar`) and `build-tools`
- Android NDK (the script installs `27.3.13750724` under `<sdk>/ndk` if missing)
- CMake 3.22+ and Ninja
- A JDK with `keytool` (Android Studio's bundled `jbr` is detected)

The APK contains `lib/<abi>/libinsland.so`, the manifest in
`android/AndroidManifest.xml` and the demo assets (`model.glb`,
`draco_box.glb`, `demo.scene.json`). At startup `android_entry.cpp` waits for
the `ANativeWindow`, extracts the assets into the app's private directory and
calls `insland_android_main`. The engine renders through the OpenGL/GLES
backend.

From VS Code, **Terminal > Run Task** offers `Insland: Android APK`
(arm64-v8a) and `Insland: Android APK (emulator)` (x86_64); both run the same
script in a dedicated terminal panel.

The same Android CMake target can be used from Android Studio by pointing a
CMake-based module at the repository root and the `InslandGame` target; the
manifest is a plain `NativeActivity` manifest with
`android.app.lib_name="insland"`.

---

## Running the demo

```
InslandGame [options] [model.gltf|model.glb]
```

| Option | Description |
| --- | --- |
| `--frames N` | Render N frames and quit |
| `--screenshot PATH` | Save the last frame as PNG |
| `--validate` | Render one frame and fail if the image is empty |
| `--no-anim` | Load animations but do not play them |
| `--no-skin` | Disable skeletal skinning (debug) |
| `--only-model` | Draw only the loaded model (no world, sky or cube) |
| `--world` / `--no-world` | Toggle the procedural island |
| `--sky` / `--no-sky` | Toggle the raymarched sky |
| `--ui` / `--no-ui` | Toggle the ImGui inspector |
| `--pick` | Click the terrain to cast a ray and print the hit |
| `--pick-at X Y` | Cast a ray at pixel `(X, Y)`, print the hit, exit 0 on hit / 5 on miss |
| `--scene PATH` | Load a JSON scene instead of the built-in demo |
| `--save-scene PATH` | Write the current demo/scene as JSON (quits unless a run mode is given) |
| `--seed N` | Procedural world seed (default `1337`) |
| `--width N --height N` | Window resolution (default `1280x720`) |

Interactive controls:

- **Left mouse drag** – orbit the camera
- **Mouse wheel** – zoom
- **Arrow keys** – orbit the camera
- **F1 / F2 / F3** – toggle world / sky / inspector
- **Escape** – quit

### Regression checks

```powershell
# D3D11: load the bundled model, render 30 frames, write a screenshot
cd build
.\InslandGame.exe --frames 30 --only-model --screenshot d3d_regress.png

# Validation: fail when the frame contains no geometry
.\InslandGame.exe --validate --frames 30 --only-model

# Picking: must report a terrain hit
.\InslandGame.exe --pick-at 640 500 --no-ui

# Draco: the compressed asset must load and produce the same bounds
.\InslandGame.exe --frames 30 --only-model --screenshot draco.png ..\workspace\game\assets\draco_box.glb
```

---

## Using the engine

```cpp
#include "insland/insland.hpp"
using namespace insland;

int main()
{
    Application app("My Game", 1280, 720);
    World world;
    Model model;

    AppCallbacks callbacks;
    callbacks.on_start = [&] {
        auto& engine = app.engine();
        engine.set_sky_enabled(true);
        world.generate(engine);
        model = engine.load_model(GLTF, "assets/model.glb");
        if (model.valid()) model.play(0, true);
    };
    callbacks.on_update = [&](float dt) {
        auto& engine = app.engine();
        if (input::key_pressed(input::Key::Escape)) app.quit();

        engine.camera().orbit(35.0f, 22.0f, 52.0f, Vec3(0.0f, 8.0f, 0.0f));
        world.draw(engine);
        engine.draw(model, Transform{});
    };

    app.set_callbacks(callbacks);
    if (!app.init()) return 1;
    return app.run();
}
```

`#include "insland/insland.hpp"` pulls in the public API. The lower-level
headers (`engine/engine.hpp`, `world/world.hpp`, ...) can also be included
individually.

### Picking

```cpp
raycast::Ray ray = raycast::screen_ray(engine.camera(), mouse_x, mouse_y,
                                       width, height,
                                       engine.camera().zero_to_one_ndc);
raycast::Hit hit = raycast::intersect_heightmap(
    ray, 800.0f, [&](float x, float z) { return world.height_at(x, z); });
if (hit.hit) spawn_at(hit.point);
```

### Saving and loading scenes

`core/serializer.hpp` is a small JSON document model: typed helpers, a strict
parser with line/column errors, a version header and atomic writes.

```cpp
JsonValue doc = JsonValue::object();
doc["name"] = "island";
doc["seed"] = 7;
doc["spawn"] = to_json(Transform{});
save_document("scene.json", doc);

if (auto loaded = load_document("scene.json"))
    seed = (*loaded)["seed"].as_int();
```

`Scene` builds on it to describe a complete drawable scene:

```cpp
Scene scene;
if (!scene.load("assets/demo.scene.json")) return 1;
scene.generate_world(engine);
scene.draw(engine);

SceneEntity& cube = scene.add("Cube", "", Transform{});
cube.color = Color::red();
scene.save("assets/demo.scene.json");
```

The demo exposes the same format:

```powershell
cd build
.\InslandGame.exe --scene ..\workspace\game\assets\demo.scene.json --frames 60
.\InslandGame.exe --save-scene my.scene.json --frames 1
```

Entities reference models by path (`"model": "model.glb"`); models are loaded
lazily and shared between entities. The document carries
`"format": "insland"` and `"version": 1`, and a newer version is rejected with a
clear error instead of being misread.

---

## Creating a new project

### Visual Studio Code

Run **Terminal > Run Task... > Insland: New Project** (or `Ctrl+Shift+B` if you
bind it). VS Code asks for a project name, creates `projects/<name>/`, then
reconfigures and builds with the active preset. The new target appears in the
CMake status bar, ready to run.

### Terminal

```powershell
py scripts/new_project.py my_game
cmake --preset windows-d3d11
cmake --build --preset windows-d3d11 --target Insland_my_game
```

The script creates:

```
projects/my_game/
    main.cpp        # ~60 line starter: world, sky, orbit camera, inspector
    CMakeLists.txt  # target linked against the engine
    assets/
```

The generated `main.cpp` is intentionally short (the orbit-camera and ImGui
lines are elided here):

```cpp
#include "insland/insland.hpp"
#include "imgui.h"
using namespace insland;

int main()
{
    Application app("My Game");          // window, engine and loop in one object
    Engine& engine = app.engine();
    World world;
    UI ui;

    float yaw = 35.0f, pitch = 22.0f, distance = 52.0f;
    const Vec3 center{0.0f, 8.0f, 0.0f};

    AppCallbacks cb;
    cb.on_start = [&] {
        engine.set_sky_enabled(true);
        world.generate(engine);
        ui.init(*engine.renderer());
    };

    cb.on_update = [&](float dt) {
        if (input::key_pressed(input::Key::Escape)) app.quit();
        engine.camera().orbit(yaw, pitch, distance, center);
        world.draw(engine);
        engine.draw_cube(Transform{});
        // ... fps + memory inspector ...
    };

    cb.on_shutdown = [&] { ui.shutdown(); };
    app.set_callbacks(cb);
    return app.init() ? app.run() : 1;
}
```

`projects/sample` is a longer reference project (animation, picking, marker).
Any directory under `projects/` that contains a `CMakeLists.txt` is picked up
automatically; the `CONFIGURE_DEPENDS` glob makes CMake notice new projects on
the next build without a manual reconfigure.

---

## Repository layout

```
source/
    insland/insland.hpp     umbrella header (public API)
    core/                   types, filesystem, logger, memory, serializer
    engine/                 engine, camera, model
    app/                    Application (fixed-step loop)
    assets/                 asset manager, Draco loader
    graphics/               raymarched sky
    raycast/                ray utilities
    scene/                  JSON-backed Scene + entities
    world/                  procedural island
    ui/                     ImGui backend
    platform/               window, renderer and input per platform
    pipeline.cpp            mesh shader + pipeline
workspace/game/             the demo (main.cpp, android_entry.cpp, assets)
projects/                   standalone sample games
android/                    NativeActivity manifest used by the APK build
scripts/                    project scaffolding and build_android.py
CMakePresets.json           Ninja presets used by the CLI and VS Code
.vscode/                    shared CMake Tools settings and tasks
external/                   Diligent, glm, tinygltf, draco, ozz, imgui, stb
```

---

## Notes for contributors

- The mesh, sky and UI shaders are HLSL and are converted to GLSL by Diligent
  for the OpenGL/GLES backend. Avoid C-style casts and backend-specific
  intrinsics; use `#ifdef GLSL` when the two languages disagree (see
  `source/pipeline.cpp`).
- Diligent's HLSL→GLSL converter names varyings after the owning parameter.
  The pixel-shader parameter **must** be named `VSMain` so it links against the
  vertex entry point on GLES 3.0 (which has no explicit `layout(location)`).
- OpenGL returns framebuffer rows bottom-up; `capture_frame` flips them using
  `NDC.YtoVScale`.
- Keep both `build` (D3D11) and `build-gl` (OpenGL) green before committing
  shader or renderer changes.
