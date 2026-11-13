// ---------------------------------------------------------------------------
// Insland Engine - sample project
// ---------------------------------------------------------------------------
// A complete game skeleton built on insland::Application:
//
//   * fixed-step simulation (on_fixed_update) + variable rendering (on_update)
//   * procedural island, raymarched sky and inspector
//   * optional glTF model with animation
//   * click-to-pick ray casting against the terrain
//
// Build:  cmake --build build --target Insland_sample
// Run:    ./build/InslandSample
// ---------------------------------------------------------------------------

#include "insland/insland.hpp"

#include "imgui.h"

#include <cstdio>
#include <string>
#include <utility>

using namespace insland;

namespace
{

struct Sample
{
    Application app{[] {
        AppConfig config;
        config.title = "Insland Sample";
        config.width = 1280;
        config.height = 720;
        config.asset_root = "assets";
        return config;
    }()};

    World world;
    Model model;
    UI ui;

    float orbit_yaw = 35.0f;
    float orbit_pitch = 22.0f;
    float orbit_distance = 52.0f;
    Vec3 orbit_center{0.0f, 8.0f, 0.0f};

    raycast::Hit last_pick;
    bool has_pick = false;
    bool show_world = true;
    bool show_model = true;
};

} // namespace

int main()
{
    Log::SetLevel(LogLevel::Info);

    Sample sample;
    Application& app = sample.app;
    Engine& engine = app.engine();

    AppCallbacks callbacks;

    callbacks.on_start = [&] {
        engine.set_sky_enabled(true);
        engine.set_sun_direction(Vec3(0.32f, 0.62f, 0.72f), 1.0f);

        WorldConfig world_config;
        world_config.seed = 7;
        sample.world.generate(engine, world_config);

        const std::string model_path = fs::locate_asset(
            "model.glb", {"../workspace/game/assets", "workspace/game/assets"});
        if (!model_path.empty())
        {
            sample.model = engine.load_model(GLTF, model_path);
            if (sample.model.valid())
            {
                sample.model.play(0, true);
                std::printf("sample: loaded %s (%zu meshes)\n", model_path.c_str(),
                            sample.model.mesh_count());
            }
        }

        if (!sample.ui.init(*engine.renderer()))
            std::printf("sample: UI unavailable, continuing without inspector\n");
    };

    callbacks.on_fixed_update = [&](float fixed) {
        // Put deterministic simulation here.  `fixed` is always 1/60 s.
        (void)fixed;
    };

    callbacks.on_update = [&](float dt) {
        if (input::key_pressed(input::Key::Escape)) app.quit();
        if (input::key_pressed(input::Key::F1)) sample.show_world = !sample.show_world;
        if (input::key_pressed(input::Key::F2))
            engine.set_sky_enabled(!engine.sky_enabled());
        if (input::key_pressed(input::Key::F3) && sample.ui.initialized())
            sample.show_model = !sample.show_model;

        const bool ui_wants_mouse =
            sample.ui.initialized() && sample.ui.wants_mouse();

        if (!ui_wants_mouse && input::mouse_down(input::MouseButton::Left))
        {
            sample.orbit_yaw += input::mouse.delta_x * 0.35f;
            sample.orbit_pitch -= input::mouse.delta_y * 0.35f;
        }
        if (!ui_wants_mouse)
        {
            sample.orbit_distance -= input::mouse.wheel * 0.6f;
            sample.orbit_distance = clampf(sample.orbit_distance, 1.5f, 400.0f);
        }
        sample.orbit_pitch = clampf(sample.orbit_pitch, -80.0f, 80.0f);

        if (input::key_down(input::Key::Left)) sample.orbit_yaw -= 60.0f * dt;
        if (input::key_down(input::Key::Right)) sample.orbit_yaw += 60.0f * dt;
        if (input::key_down(input::Key::Up)) sample.orbit_pitch += 45.0f * dt;
        if (input::key_down(input::Key::Down)) sample.orbit_pitch -= 45.0f * dt;

        engine.camera().orbit(sample.orbit_yaw, sample.orbit_pitch,
                              sample.orbit_distance, sample.orbit_center);

        // Click to pick the terrain.
        if (sample.show_world && !ui_wants_mouse &&
            input::mouse.was_pressed(input::MouseButton::Left))
        {
            const raycast::Ray ray = raycast::screen_ray(
                engine.camera(), input::mouse.x, input::mouse.y,
                static_cast<float>(app.config().width),
                static_cast<float>(app.config().height),
                engine.camera().zero_to_one_ndc);
            sample.last_pick = raycast::intersect_heightmap(
                ray, 800.0f,
                [&](float x, float z) { return sample.world.height_at(x, z); });
            sample.has_pick = sample.last_pick.hit;
            if (sample.has_pick)
                std::printf("sample: picked (%.2f %.2f %.2f)\n",
                            sample.last_pick.point.x, sample.last_pick.point.y,
                            sample.last_pick.point.z);
        }

        // Draw -----------------------------------------------------------------
        if (sample.show_world) sample.world.draw(engine);

        if (sample.has_pick)
        {
            Transform marker;
            marker.position = sample.last_pick.point + sample.last_pick.normal * 0.4f;
            marker.scale = Vec3(0.45f);
            engine.draw_cube(marker, Color{0.95f, 0.85f, 0.2f, 1.0f});
        }

        if (sample.model.valid() && sample.show_model)
        {
            Transform transform;
            transform.rotation = glm::angleAxis(static_cast<float>(engine.time()) * 0.25f,
                                                Vec3(0.0f, 1.0f, 0.0f));
            engine.draw(sample.model, transform);
        }

        // Inspector ------------------------------------------------------------
        if (sample.ui.initialized())
        {
            sample.ui.begin_frame(app.config().width, app.config().height, dt);

            if (ImGui::Begin("Sample Inspector"))
            {
                ImGui::Text("frame %llu", static_cast<unsigned long long>(engine.frame_index()));
                ImGui::Separator();
                ImGui::TextUnformatted(Memory::summary().c_str());
                ImGui::Separator();
                ImGui::Checkbox("World", &sample.show_world);
                bool sky = engine.sky_enabled();
                if (ImGui::Checkbox("Sky", &sky)) engine.set_sky_enabled(sky);
                ImGui::Checkbox("Model", &sample.show_model);
                if (sample.has_pick)
                    ImGui::Text("pick: %.2f %.2f %.2f", sample.last_pick.point.x,
                                sample.last_pick.point.y, sample.last_pick.point.z);
            }
            ImGui::End();

            sample.ui.end_frame(*engine.renderer());
        }
    };

    callbacks.on_shutdown = [&] { sample.ui.shutdown(); };

    app.set_callbacks(std::move(callbacks));

    if (!app.init())
    {
        std::fprintf(stderr, "sample: failed to initialise the engine\n");
        return 1;
    }

    return app.run();
}
