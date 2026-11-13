#pragma once

// ---------------------------------------------------------------------------
// Insland Engine - application layer (adapted from the AetherEngine design)
// ---------------------------------------------------------------------------
// Composition-based lifecycle: the game supplies callbacks instead of deriving
// from a deep class hierarchy.  A few functions give the game a complete loop:
//
//     insland::Application app("My Game");
//     AppCallbacks callbacks;
//     callbacks.on_start        = [] { /* create world */ };
//     callbacks.on_update       = [&](float dt) { /* gameplay; draw here */ };
//     callbacks.on_fixed_update = [&](float fixed) { /* simulation steps  */ };
//     app.set_callbacks(callbacks);
//     app.init();
//     return app.run();
//
// The loop mirrors Aether::Application: fixed-step simulation with a variable
// rendering rate, clamped deltas and a safety valve for the accumulator.
// ---------------------------------------------------------------------------

#include <functional>
#include <string>

#include "engine/engine.hpp"

namespace insland
{

struct AppConfig
{
    std::string title = "Insland";
    int width = 1280;
    int height = 720;
    bool resizable = true;
    bool fullscreen = false;
    bool vsync = true;
    bool validation = false;

    // Root for game content, forwarded to Engine::Config and fs.
    std::string asset_root;

    Color clear_color{0.05f, 0.062f, 0.075f, 1.0f};

    // Fixed simulation step in seconds.  0 disables the fixed-step loop.
    float fixed_timestep = 1.0f / 60.0f;

    // Maximum frame delta accepted by the loop.  Prevents a debugger pause or
    // an Android resume from generating hundreds of fixed steps.
    float max_frame_time = 0.25f;

    // Safety valve for the fixed-step accumulator.
    int max_fixed_steps_per_frame = 8;
};

/// Composition-based application callbacks.
struct AppCallbacks
{
    std::function<void()>                 on_start;
    std::function<void(float fixed_delta)> on_fixed_update;
    std::function<void(float delta)>      on_update;   // draw the frame here (or in on_render)
    std::function<void(float delta)>      on_render;   // optional second pass before end_frame
    std::function<void(int, int)>         on_resize;
    std::function<void()>                 on_suspend;   // window minimized / hidden
    std::function<void()>                 on_resume;
    std::function<void()>                 on_shutdown;
};

/// Owns the engine (window, renderer, clock) and drives the game loop.
///
///     insland::Application app(config);
///     app.set_callbacks({...});
///     app.init();
///     return app.run();     // 0 on a normal exit
class Application
{
public:
    explicit Application(AppConfig config = {});

    /// Shorthand for the common case: title plus optional window size.
    ///
    ///     insland::Application app("My Game");
    Application(std::string title, int width = 1280, int height = 720);

    ~Application();

    Application(const Application&) = delete;
    Application& operator=(const Application&) = delete;

    /// Creates the window and graphics backend.
    bool init();

    /// Runs until quit() or the window closes.  Returns the process exit code.
    int run();

    /// Stops the loop cleanly at the end of the current frame.
    void quit() { running_ = false; }

    void set_callbacks(AppCallbacks callbacks) { callbacks_ = std::move(callbacks); }

    bool initialized() const { return initialized_; }
    bool is_running() const { return running_; }

    Engine& engine() { return engine_; }
    const Engine& engine() const { return engine_; }
    const AppConfig& config() const { return config_; }

private:
    AppConfig config_;
    AppCallbacks callbacks_;

    Engine engine_;
    Camera camera_;

    bool initialized_ = false;
    bool running_ = false;
};

} // namespace insland
