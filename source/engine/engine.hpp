#pragma once

// ---------------------------------------------------------------------------
// Insland Engine - public API
// ---------------------------------------------------------------------------
//   using namespace insland;
//
//   if (!engine.init()) return 1;
//   Model cube  = engine.create_cube();
//   Model model = engine.load_model(GLTF, "assets/model.gltf");
//
//   while (engine.running())
//   {
//       const float dt = engine.begin_frame();
//       cube.rotation ...
//       engine.draw(cube, transform);
//       engine.end_frame();
//   }
//   engine.shutdown();
// ---------------------------------------------------------------------------

#include <cstdint>
#include <string>
#include <vector>

#include "core/types.hpp"
#include "engine/camera.hpp"
#include "engine/model.hpp"
#include "platform/keyboard.hpp"
#include "platform/mouse.hpp"
#include "platform/touch.hpp"

namespace insland
{

enum class ModelFormat
{
    GLTF,
    GLB
};

inline constexpr ModelFormat GLTF = ModelFormat::GLTF;
inline constexpr ModelFormat GLB = ModelFormat::GLB;

// Procedural mesh used by the world generator (and any game code that wants to
// build geometry at runtime).  Normals, uvs and colors are optional; missing
// data is filled with sensible defaults (flat up normal, white color).
struct MeshData
{
    std::vector<Vec3> positions;
    std::vector<Vec3> normals;
    std::vector<Vec2> uvs;
    std::vector<Vec4> colors;                 // per-vertex tint; empty means white
    std::vector<std::uint32_t> indices;       // triangle list
    Color tint{1.0f, 1.0f, 1.0f, 1.0f};
};

namespace platform
{
class Window;
class Renderer;
}

class Engine
{
public:
    struct Config
    {
        std::string title = "Insland Engine";
        std::uint32_t width = 1280;
        std::uint32_t height = 720;
        bool vsync = true;
        bool fullscreen = false;
        bool resizable = true;
        bool validation = false;

        Color clear_color{0.055f, 0.062f, 0.078f, 1.0f};

        // Directory used to resolve relative model paths (searched in addition
        // to the working directory and the executable directory).
        std::string asset_root;

        // Android: ANativeWindow* supplied by the activity.  Ignored on desktop.
        void* native_window = nullptr;
    };

    Engine();
    ~Engine();

    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;

    // Overload instead of a default argument: clang rejects
    // `const Config& config = Config{}` inside the class because the nested
    // Config's default member initializers are not complete yet.
    bool init();
    bool init(const Config& config);
    void shutdown();

    bool initialized() const;
    bool running() const;

    // Processes platform events.  begin_frame() calls this automatically.
    void poll_events();

    // Starts a frame: polls events, updates the clock and animations, clears
    // the swap chain.  Returns the elapsed time in seconds.
    float begin_frame();

    // Presents the frame and clears per-frame input state.
    void end_frame();

    // Advances all models that were loaded by this engine.
    void update(float delta_time);

    // Asset loading ---------------------------------------------------------
    Model load_model(ModelFormat format, const std::string& path);
    Model load_model(const std::string& path);

    // Creates a unit cube centred at the origin.  The cube uses the same
    // pipeline as loaded glTF meshes.
    Model create_cube(float size = 1.0f,
                      const Color& color = Color{0.82f, 0.42f, 0.24f, 1.0f});

    // Creates a model from procedural geometry.  The returned handle can be
    // drawn with draw() just like any loaded asset.
    Model create_model(const MeshData& geometry);

    // Drawing ---------------------------------------------------------------
    void draw(const Model& model, const Transform& transform = Transform{});
    void draw(const Model& model, const Mat4& transform);
    void draw_cube(const Transform& transform,
                   const Color& color = Color{0.82f, 0.42f, 0.24f, 1.0f});

    // State -----------------------------------------------------------------
    Camera& camera() { return camera_; }
    const Camera& camera() const { return camera_; }

    void set_clear_color(const Color& color);
    const Color& clear_color() const;

    // Raymarched sky ---------------------------------------------------------
    void set_sky_enabled(bool enabled);
    bool sky_enabled() const;
    void set_sun_direction(const Vec3& direction, float intensity = 1.0f);

    // Habilita/desabilita skinning esquelético (útil para depuração de modelos
    // que aparecem distorcidos).
    void set_skin_enabled(bool enabled);
    bool skin_enabled() const;

    float aspect_ratio() const;
    double time() const;
    float delta_time() const;
    std::uint64_t frame_index() const;

    // Saves the current back buffer to a PNG file.  Call after drawing and
    // before end_frame().
    bool capture_screenshot(const std::string& path);

    // Returns true when the last captured back buffer contains at least one
    // pixel that differs from the clear color (i.e. something was drawn).
    bool validate_last_frame(const Color& expected_clear);

    platform::Window* window();
    platform::Renderer* renderer();

    const std::vector<std::string>& asset_search_paths() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    Camera camera_;
};

// The global engine instance.  With `using namespace insland;` this enables the
// exact API shown in the documentation: engine.init(), engine.load_model(...).
extern Engine engine;

} // namespace insland
