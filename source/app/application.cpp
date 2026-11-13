// ---------------------------------------------------------------------------
// Insland Engine - application layer implementation (adapted from AetherEngine)
// ---------------------------------------------------------------------------

#include "app/application.hpp"

#include "core/logger.hpp"

namespace insland
{

Application::Application(AppConfig config) : config_(std::move(config))
{
    Log::Debug("App", "Application created: ", config_.title);
}

Application::Application(std::string title, int width, int height)
    : Application(AppConfig{std::move(title), width, height})
{
}

Application::~Application()
{
    if (initialized_)
    {
        if (callbacks_.on_shutdown) callbacks_.on_shutdown();
        engine_.shutdown();
    }
}

bool Application::init()
{
    Engine::Config engine_config;
    engine_config.title = config_.title;
    engine_config.width = config_.width;
    engine_config.height = config_.height;
    engine_config.resizable = config_.resizable;
    engine_config.fullscreen = config_.fullscreen;
    engine_config.vsync = config_.vsync;
    engine_config.validation = config_.validation;
    engine_config.clear_color = config_.clear_color;
    engine_config.asset_root = config_.asset_root;

    if (!engine_.init(engine_config))
        return false;

    initialized_ = true;
    running_ = true;
    return true;
}

int Application::run()
{
    if (!initialized_)
        return -1;

    if (callbacks_.on_start) callbacks_.on_start();

    const float fixed = config_.fixed_timestep;
    double accumulator = 0.0;

    while (running_ && engine_.running())
    {
        const float delta = engine_.begin_frame();
        if (!engine_.running())
            break;

        if (fixed > 0.0f)
        {
            accumulator += config_.max_frame_time > 0.0f ? std::min(delta, config_.max_frame_time) : delta;
            int steps = 0;
            while (accumulator >= fixed && steps < config_.max_fixed_steps_per_frame)
            {
                if (callbacks_.on_fixed_update) callbacks_.on_fixed_update(fixed);
                accumulator -= fixed;
                ++steps;
            }
            if (steps == config_.max_fixed_steps_per_frame)
                accumulator = 0.0; // drop the backlog instead of spiralling
        }

        if (callbacks_.on_update) callbacks_.on_update(delta);
        if (callbacks_.on_render) callbacks_.on_render(delta);

        engine_.end_frame();
    }

    if (callbacks_.on_shutdown) callbacks_.on_shutdown();
    engine_.shutdown();
    initialized_ = false;
    return 0;
}

} // namespace insland
