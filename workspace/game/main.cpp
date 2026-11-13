// ---------------------------------------------------------------------------
// Insland Engine - demo
// ---------------------------------------------------------------------------
//   InslandGame [options] [model.gltf|model.glb]
//
// Options:
//   --frames N            quit after N rendered frames
//   --screenshot PATH     save the last rendered frame as a PNG
//   --validate            load the model, render one frame and quit
//   --no-anim             load model but do not play animations
//   --no-skin             load model but disable skeletal skinning (debug)
//   --only-model          draw only the loaded model (no world/cube)
//   --world / --no-world  procedural island on/off
//   --sky / --no-sky      raymarched sky and clouds on/off
//   --ui / --no-ui        ImGui inspector on/off
//   --pick                click the terrain to cast a ray and print the hit
//   --pick-at X Y         cast a ray at pixel (X, Y), print the hit and quit
//                         (exit code 0 on a hit, 5 on a miss)
//   --scene PATH          load a JSON scene instead of the built-in demo
//   --save-scene PATH     write the current scene as JSON (and quit unless a
//                         run mode such as --frames is also given)
//   --seed N              procedural world seed (default 1337)
//   --width N --height N  window resolution (default 1280x720)
//
// Draws a procedural island (terrain, water, trees) under a raymarched sky,
// plus a rotating cube and an optional glTF/GLB model.  F1 toggles the world,
// F2 toggles the sky and F3 toggles the inspector.
// ---------------------------------------------------------------------------

#include "insland/insland.hpp"

#include "engine/model_data.hpp"

#if defined(__ANDROID__)
#    include "platform/android.hpp"
#endif

#include "imgui.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <string>
#include <vector>

using namespace insland;

namespace
{

int print_usage()
{
    std::printf("usage: InslandGame [--frames N] [--screenshot PATH] [--validate]\n");
    std::printf("                   [--no-anim] [--no-skin] [--only-model]\n");
    std::printf("                   [--world|--no-world] [--sky|--no-sky] [--ui|--no-ui]\n");
    std::printf("                   [--pick] [--pick-at X Y] [--seed N]\n");
    std::printf("                   [--scene PATH] [--save-scene PATH]\n");
    std::printf("                   [--width N] [--height N] [model.gltf|model.glb]\n");
    return 0;
}

int run_demo(int argc, char** argv)
{
    std::fflush(stdout);

    std::string model_path;
    std::string screenshot_path;
    std::string scene_path;
    std::string save_scene_path;
    int frame_limit = 0;
    bool validate = false;
    bool no_anim = false;
    bool no_skin = false;
    bool only_model = false;
    bool pick_mode = false;
    bool pick_at_enabled = false;
    float pick_at_x = 0.0f;
    float pick_at_y = 0.0f;
    int width = 1280;
    int height = 720;
    std::uint32_t seed = 1337;
    bool seed_set = false;

    // -1 means "use the default for this run mode".
    int world_flag = -1;
    int sky_flag = -1;
    int ui_flag = -1;

    for (int i = 1; i < argc; ++i)
    {
        const char* arg = argv[i];
        if (std::strcmp(arg, "--help") == 0 || std::strcmp(arg, "-h") == 0)
        {
            return print_usage();
        }
        else if (std::strcmp(arg, "--frames") == 0 && i + 1 < argc)
        {
            frame_limit = std::atoi(argv[++i]);
        }
        else if (std::strcmp(arg, "--screenshot") == 0 && i + 1 < argc)
        {
            screenshot_path = argv[++i];
        }
        else if (std::strcmp(arg, "--validate") == 0)
        {
            validate = true;
        }
        else if (std::strcmp(arg, "--no-anim") == 0)
        {
            no_anim = true;
        }
        else if (std::strcmp(arg, "--no-skin") == 0)
        {
            no_skin = true;
        }
        else if (std::strcmp(arg, "--only-model") == 0)
        {
            only_model = true;
        }
        else if (std::strcmp(arg, "--world") == 0)
        {
            world_flag = 1;
        }
        else if (std::strcmp(arg, "--no-world") == 0)
        {
            world_flag = 0;
        }
        else if (std::strcmp(arg, "--sky") == 0)
        {
            sky_flag = 1;
        }
        else if (std::strcmp(arg, "--no-sky") == 0)
        {
            sky_flag = 0;
        }
        else if (std::strcmp(arg, "--ui") == 0)
        {
            ui_flag = 1;
        }
        else if (std::strcmp(arg, "--no-ui") == 0)
        {
            ui_flag = 0;
        }
        else if (std::strcmp(arg, "--pick") == 0)
        {
            pick_mode = true;
        }
        else if (std::strcmp(arg, "--pick-at") == 0 && i + 2 < argc)
        {
            pick_at_enabled = true;
            pick_at_x = static_cast<float>(std::atof(argv[++i]));
            pick_at_y = static_cast<float>(std::atof(argv[++i]));
        }
        else if (std::strcmp(arg, "--seed") == 0 && i + 1 < argc)
        {
            seed = static_cast<std::uint32_t>(std::strtoul(argv[++i], nullptr, 10));
            seed_set = true;
        }
        else if (std::strcmp(arg, "--scene") == 0 && i + 1 < argc)
        {
            scene_path = argv[++i];
        }
        else if (std::strcmp(arg, "--save-scene") == 0 && i + 1 < argc)
        {
            save_scene_path = argv[++i];
        }
        else if (std::strcmp(arg, "--width") == 0 && i + 1 < argc)
        {
            width = std::atoi(argv[++i]);
        }
        else if (std::strcmp(arg, "--height") == 0 && i + 1 < argc)
        {
            height = std::atoi(argv[++i]);
        }
        else if (arg[0] != '-')
        {
            model_path = arg;
        }
        else
        {
            std::fprintf(stderr, "Insland: unknown option %s\n", arg);
            return print_usage();
        }
    }

    if (validate && frame_limit == 0) frame_limit = 1;

    // Regression / headless runs keep the extra systems off so the frame stays
    // deterministic unless they are explicitly requested.
    bool world_enabled = world_flag >= 0 ? world_flag != 0 : !only_model;
    bool sky_enabled = sky_flag >= 0 ? sky_flag != 0 : !only_model;
    bool ui_enabled = ui_flag >= 0 ? ui_flag != 0 : (!only_model && !validate);

    const bool scene_mode = !scene_path.empty();

    Engine::Config config;
    config.title = scene_mode
                       ? "Insland Engine - " + scene_path
                       : (model_path.empty() ? "Insland Engine" : "Insland Engine - " + model_path);
    config.width = width > 0 ? static_cast<std::uint32_t>(width) : 1280;
    config.height = height > 0 ? static_cast<std::uint32_t>(height) : 720;
    config.clear_color = Color{0.055f, 0.062f, 0.078f, 1.0f};

#if defined(__ANDROID__)
    config.native_window = platform::android_native_window();
    config.asset_root = platform::android_asset_root();
#endif

    Log::SetLevel(LogLevel::Info);

    std::printf("Insland: initializing engine...\n");
    std::fflush(stdout);
    if (!engine.init(config))
    {
        std::fprintf(stderr, "Insland: engine.init() failed\n");
        return 1;
    }

    engine.set_skin_enabled(!no_skin);
    engine.set_sky_enabled(sky_enabled);
    engine.set_sun_direction(Vec3(0.32f, 0.62f, 0.72f), 1.0f);

    std::printf("Insland Engine running (%ux%u).\n", config.width, config.height);
    std::printf("  Left mouse + drag : orbit camera\n");
    std::printf("  Mouse wheel       : zoom\n");
    std::printf("  Arrow keys        : orbit camera\n");
    std::printf("  F1/F2/F3          : world / sky / inspector\n");
    std::printf("  Escape            : quit\n");

    // Scene -----------------------------------------------------------------
    Scene scene;
    if (scene_mode)
    {
        if (!scene.load(scene_path))
        {
            std::fprintf(stderr, "Insland: failed to load scene %s\n", scene_path.c_str());
            engine.shutdown();
            return 6;
        }

        if (seed_set) scene.world_config.seed = seed;
        world_enabled = scene.world_enabled;
        sky_enabled = scene.sky_enabled;
        engine.set_sky_enabled(sky_enabled);
    }

    // World -----------------------------------------------------------------
    World world;
    WorldConfig world_config;
    world_config.seed = seed;
    if (scene_mode) world_config = scene.world_config;
    if (world_enabled)
    {
        if (scene_mode)
        {
            if (!scene.generate_world(engine))
            {
                std::fprintf(stderr, "Insland: world generation failed\n");
                world_enabled = false;
            }
        }
        else
        {
            std::printf("Generating island (seed %u)...\n", seed);
            if (!world.generate(engine, world_config))
            {
                std::fprintf(stderr, "Insland: world generation failed\n");
                world_enabled = false;
            }
        }
    }

    // UI --------------------------------------------------------------------
    UI ui;
    if (ui_enabled && !ui.init(*engine.renderer()))
    {
        std::fprintf(stderr, "Insland: UI init failed, continuing without inspector\n");
        ui_enabled = false;
    }

    // Model -----------------------------------------------------------------
    Model model;
    if (!scene_mode && model_path.empty())
    {
        // Automatically load the bundled asset when no model is given on the
        // command line.  Several paths are tried so the demo also works from
        // the build directory and from an installed layout.
        const std::vector<std::string> candidates = {
#if defined(__ANDROID__)
            fs::join(config.asset_root, "model.glb"),
#endif
            fs::join(fs::join(fs::current_directory(), "assets"), "model.glb"),
            fs::join(fs::join(fs::executable_directory(), "assets"), "model.glb"),
            fs::join(fs::join(fs::executable_directory(), ".."), "workspace/game/assets/model.glb"),
            "../workspace/game/assets/model.glb",
            "workspace/game/assets/model.glb",
            "assets/model.glb",
        };
        for (const auto& cand : candidates)
        {
            if (fs::exists(cand))
            {
                model_path = cand;
                break;
            }
        }
        if (model_path.empty())
        {
            model_path = candidates.front();
            std::printf("No model found in asset paths, will attempt: %s\n", model_path.c_str());
        }
        else
        {
            std::printf("Auto-loading model: %s\n", model_path.c_str());
        }
    }

    if (!scene_mode && !model_path.empty())
    {
        std::printf("Loading %s ...\n", model_path.c_str());
        model = engine.load_model(GLTF, model_path);
        if (model.valid())
        {
            const Bounds bounds = model.bounds();
            std::printf("  loaded '%s': %zu meshes, %zu animations\n", model.name().c_str(),
                        model.mesh_count(), model.animation_count());
            if (bounds.valid())
                std::printf("  bounds: min=(%.3f %.3f %.3f) max=(%.3f %.3f %.3f)\n", bounds.min.x,
                            bounds.min.y, bounds.min.z, bounds.max.x, bounds.max.y, bounds.max.z);
            for (std::size_t i = 0; i < model.animation_count(); ++i)
                std::printf("  animation %zu: %s\n", i, model.animation_name(i).c_str());

            if (!no_anim && model.animation_count() > 0) model.play(0, true);
        }
        else
        {
            std::fprintf(stderr, "Failed to load model: %s\n", model_path.c_str());
            if (validate)
            {
                engine.shutdown();
                return 2;
            }
        }
    }

    // Scene transforms ------------------------------------------------------
    const float model_ground =
        world_enabled ? (scene_mode ? scene.height_at(6.0f, 0.0f) : world.height_at(6.0f, 0.0f))
                      : 0.0f;

    Transform cube_transform;
    cube_transform.position = Vec3(-1.4f, 0.65f, 0.0f);
    cube_transform.scale = Vec3(0.9f);

    Transform model_transform;
    if (model.valid())
    {
        const Bounds bounds = model.bounds();
        if (bounds.valid())
        {
            const Vec3 size = bounds.size();
            const float largest = std::max(size.x, std::max(size.y, size.z));
            const float scale = largest > 0.0001f ? 2.4f / largest : 1.0f;
            const float model_x = world_enabled ? 6.0f : 1.2f;
            model_transform.position = Vec3(model_x, model_ground - bounds.min.y * scale, 0.0f);
            model_transform.scale = Vec3(scale);
        }
    }

    Transform floor_transform;
    floor_transform.position = Vec3(0.0f, -0.01f, 0.0f);
    floor_transform.scale = Vec3(7.0f, 0.02f, 7.0f);

    float orbit_yaw = 35.0f;
    float orbit_pitch = world_enabled ? 22.0f : 18.0f;
    float orbit_distance = world_enabled ? 52.0f : 7.0f;
    const Vec3 orbit_center = world_enabled ? Vec3(0.0f, 8.0f, 0.0f) : Vec3(0.0f, 0.8f, 0.0f);

    int rendered_frames = 0;
    float fps = 0.0f;

    raycast::Hit last_pick;
    bool has_pick = false;
    bool model_visible = true;

    // Save the current demo as a scene -------------------------------------
    if (!save_scene_path.empty())
    {
        Scene out;
        if (scene_mode)
        {
            out = scene;
        }
        else
        {
            out.name = "Insland Demo";
            out.world_config = world_config;
            out.world_enabled = world_enabled;
            out.sky_enabled = sky_enabled;
            out.add("Cube", "", cube_transform, Color{0.85f, 0.43f, 0.22f, 1.0f});
            if (model.valid())
                out.add("Model", fs::filename(model_path), model_transform, Color::white());
        }

        if (!out.save(save_scene_path))
        {
            ui.shutdown();
            engine.shutdown();
            return 7;
        }

        const bool run_after_save =
            frame_limit > 0 || validate || !screenshot_path.empty() || pick_at_enabled;
        if (!run_after_save)
        {
            ui.shutdown();
            engine.shutdown();
            return 0;
        }
    }

    // Non-interactive raycast test -----------------------------------------
    // Sets up the default orbit camera, casts a ray through the requested
    // pixel and reports the terrain hit.  Used by the automated checks:
    // a hit exits with 0, a miss with 5.
    if (pick_at_enabled)
    {
        const bool terrain_ready = scene_mode ? scene.world_ready() : world.valid();
        if (!world_enabled || !terrain_ready)
        {
            std::fprintf(stderr, "pick-at: the procedural world is required\n");
            ui.shutdown();
            engine.shutdown();
            return 5;
        }

        engine.camera().orbit(orbit_yaw, orbit_pitch, orbit_distance, orbit_center);
        const raycast::Ray ray = raycast::screen_ray(
            engine.camera(), pick_at_x, pick_at_y, static_cast<float>(config.width),
            static_cast<float>(config.height), engine.camera().zero_to_one_ndc);
        const raycast::Hit hit = raycast::intersect_heightmap(
            ray, 800.0f, [&](float x, float z) {
                return scene_mode ? scene.height_at(x, z) : world.height_at(x, z);
            });

        if (!hit.hit)
        {
            std::printf("pick-at: no terrain hit at (%.1f, %.1f)\n", pick_at_x, pick_at_y);
            ui.shutdown();
            engine.shutdown();
            return 5;
        }

        std::printf("pick-at: hit (%.3f %.3f %.3f) normal (%.3f %.3f %.3f) t=%.2f\n",
                    hit.point.x, hit.point.y, hit.point.z, hit.normal.x, hit.normal.y,
                    hit.normal.z, hit.distance);
        ui.shutdown();
        engine.shutdown();
        return 0;
    }

    while (engine.running())
    {
        const float dt = engine.begin_frame();
        if (!engine.running()) break;

        if (dt > 0.0f) fps = fps == 0.0f ? 1.0f / dt : fps * 0.9f + (1.0f / dt) * 0.1f;

        if (input::key_pressed(input::Key::Escape))
            break;

        if (input::key_pressed(input::Key::F1))
        {
            world_enabled = !world_enabled;
            if (scene_mode) scene.world_enabled = world_enabled;
            std::printf("world %s\n", world_enabled ? "on" : "off");
        }
        if (input::key_pressed(input::Key::F2))
        {
            engine.set_sky_enabled(!engine.sky_enabled());
            std::printf("sky %s\n", engine.sky_enabled() ? "on" : "off");
        }
        if (input::key_pressed(input::Key::F3) && ui.initialized())
        {
            ui_enabled = !ui_enabled;
            std::printf("inspector %s\n", ui_enabled ? "on" : "off");
        }

        const bool ui_wants_mouse = ui_enabled && ui.initialized() && ui.wants_mouse();

        if (!ui_wants_mouse && input::mouse_down(input::MouseButton::Left))
        {
            orbit_yaw += input::mouse.delta_x * 0.35f;
            orbit_pitch -= input::mouse.delta_y * 0.35f;
        }
        if (!ui_wants_mouse)
        {
            orbit_distance -= input::mouse.wheel * 0.6f;
            orbit_distance = clampf(orbit_distance, 1.5f, 400.0f);
        }
        orbit_pitch = clampf(orbit_pitch, -80.0f, 80.0f);

        if (input::key_down(input::Key::Left)) orbit_yaw -= 60.0f * dt;
        if (input::key_down(input::Key::Right)) orbit_yaw += 60.0f * dt;
        if (input::key_down(input::Key::Up)) orbit_pitch += 45.0f * dt;
        if (input::key_down(input::Key::Down)) orbit_pitch -= 45.0f * dt;

        engine.camera().orbit(orbit_yaw, orbit_pitch, orbit_distance, orbit_center);

        // Picking -----------------------------------------------------------
        if (pick_mode && world_enabled && !ui_wants_mouse &&
            input::mouse.was_pressed(input::MouseButton::Left))
        {
            const raycast::Ray ray = raycast::screen_ray(
                engine.camera(), input::mouse.x, input::mouse.y,
                static_cast<float>(config.width), static_cast<float>(config.height),
                engine.camera().zero_to_one_ndc);
            last_pick = raycast::intersect_heightmap(
                ray, 800.0f, [&](float x, float z) {
                    return scene_mode ? scene.height_at(x, z) : world.height_at(x, z);
                });
            has_pick = last_pick.hit;
            if (last_pick.hit)
                std::printf("pick: (%.2f %.2f %.2f) normal (%.2f %.2f %.2f) t=%.1f\n",
                            last_pick.point.x, last_pick.point.y, last_pick.point.z,
                            last_pick.normal.x, last_pick.normal.y, last_pick.normal.z,
                            last_pick.distance);
            else
                std::printf("pick: no terrain hit\n");
        }

        const float t = static_cast<float>(engine.time());
        cube_transform.rotation = glm::angleAxis(t * 0.9f, glm::normalize(Vec3(0.35f, 1.0f, 0.15f)));

        // Draw --------------------------------------------------------------
        if (scene_mode)
        {
            scene.draw(engine);
        }
        else
        {
            if (world_enabled && world.valid())
            {
                world.draw(engine);
            }
            else if (!only_model)
            {
                floor_transform.rotation = Quat(1.0f, 0.0f, 0.0f, 0.0f);
                engine.draw_cube(floor_transform, Color{0.12f, 0.13f, 0.16f, 1.0f});
            }

            if (!only_model)
                engine.draw_cube(cube_transform, Color{0.85f, 0.43f, 0.22f, 1.0f});
        }

        if (has_pick)
        {
            Transform marker;
            marker.position = last_pick.point + last_pick.normal * 0.4f;
            marker.scale = Vec3(0.45f);
            marker.rotation = glm::angleAxis(t, Vec3(0.0f, 1.0f, 0.0f));
            engine.draw_cube(marker, Color{0.95f, 0.85f, 0.2f, 1.0f});
        }

        if (model.valid() && model_visible)
        {
            model_transform.rotation = glm::angleAxis(t * 0.25f, Vec3(0.0f, 1.0f, 0.0f));
            engine.draw(model, model_transform);
        }

        // Inspector ---------------------------------------------------------
        if (ui_enabled && ui.initialized())
        {
            ui.begin_frame(static_cast<int>(config.width), static_cast<int>(config.height), dt);

            ImGui::SetNextWindowSize(ImVec2(330.0f, 0.0f), ImGuiCond_FirstUseEver);
            if (ImGui::Begin("Insland Inspector"))
            {
                ImGui::Text("FPS %.1f  (%.2f ms)", fps, fps > 0.0f ? 1000.0f / fps : 0.0f);
                ImGui::Text("frame %llu", static_cast<unsigned long long>(engine.frame_index()));
                ImGui::Separator();
                ImGui::TextUnformatted(Memory::summary().c_str());
                ImGui::Separator();

                ImGui::Checkbox("World", &world_enabled);
                if (scene_mode) scene.world_enabled = world_enabled;

                bool sky_on = engine.sky_enabled();
                if (ImGui::Checkbox("Sky", &sky_on))
                {
                    engine.set_sky_enabled(sky_on);
                    if (scene_mode) scene.sky_enabled = sky_on;
                }
                if (!scene_mode) ImGui::Checkbox("Model", &model_visible);

                if (scene_mode)
                {
                    ImGui::Text("scene: %s", scene.name.c_str());
                    ImGui::Text("%zu entities", scene.entities.size());
                }

                if (ImGui::Button("Regenerate island") && world_enabled)
                {
                    world_config.seed = seed;
                    if (scene_mode)
                    {
                        scene.world_config.seed = seed;
                        scene.generate_world(engine, true);
                    }
                    else
                    {
                        world.generate(engine, world_config);
                    }
                }
                ImGui::SameLine();
                ImGui::SetNextItemWidth(110.0f);
                if (ImGui::InputScalar("seed", ImGuiDataType_U32, &seed))
                {
                    world_config.seed = seed;
                    if (scene_mode) scene.world_config.seed = seed;
                }

                ImGui::Separator();
                if (has_pick)
                {
                    ImGui::Text("pick: %.2f %.2f %.2f", last_pick.point.x, last_pick.point.y,
                                last_pick.point.z);
                    ImGui::Text("normal: %.2f %.2f %.2f", last_pick.normal.x, last_pick.normal.y,
                                last_pick.normal.z);
                }
                else
                {
                    ImGui::TextUnformatted(pick_mode ? "pick: click the terrain"
                                                     : "pick: run with --pick");
                }
            }
            ImGui::End();

            ui.end_frame(*engine.renderer());
        }

        ++rendered_frames;

        const bool last_frame = frame_limit > 0 && rendered_frames >= frame_limit;
        const bool should_capture = !screenshot_path.empty() && (last_frame || frame_limit == 0);
        const bool validate_capture = validate && last_frame;
        if (should_capture || validate_capture)
        {
            if (frame_limit == 0) frame_limit = rendered_frames; // capture immediately
            engine.capture_screenshot(validate_capture ? "validate_frame.png" : screenshot_path);
        }

        engine.end_frame();

        if (frame_limit > 0 && rendered_frames >= frame_limit) break;
    }

    std::printf("Insland: rendered %d frames in %.2f s\n", rendered_frames, engine.time());

    if (validate)
    {
        if (!engine.validate_last_frame(engine.clear_color()))
        {
            std::fprintf(stderr, "Insland: validation FAILED - frame appears empty\n");
            engine.shutdown();
            return 4;
        }
        std::printf("Insland: validation PASSED - frame contains rendered content\n");
    }

    ui.shutdown();
    engine.shutdown();
    return 0;
}

} // namespace

#if defined(__ANDROID__)
extern "C" int insland_android_main(int argc, char** argv)
{
    try
    {
        return run_demo(argc, argv);
    }
    catch (const std::exception& error)
    {
        std::fprintf(stderr, "Insland: fatal error: %s\n", error.what());
        return 3;
    }
    catch (...)
    {
        std::fprintf(stderr, "Insland: fatal unknown error\n");
        return 3;
    }
}
#else
int main(int argc, char** argv)
{
    try
    {
        return run_demo(argc, argv);
    }
    catch (const std::exception& error)
    {
        std::fprintf(stderr, "Insland: fatal error: %s\n", error.what());
        return 3;
    }
    catch (...)
    {
        std::fprintf(stderr, "Insland: fatal unknown error\n");
        return 3;
    }
}
#endif
