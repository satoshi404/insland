// ---------------------------------------------------------------------------
// Insland Engine - Android entry point
// ---------------------------------------------------------------------------
// android_native_app_glue owns the activity lifecycle.  This file waits for the
// ANativeWindow, extracts the bundled assets into the app's private directory
// and then hands control to the regular demo (insland_android_main).
// ---------------------------------------------------------------------------

#if defined(__ANDROID__)

#    include <android/asset_manager.h>
#    include <android/log.h>
#    include <android/native_window.h>
#    include <android_native_app_glue.h>

#    include <cstdio>
#    include <string>
#    include <sys/stat.h>

#    include "platform/android.hpp"

extern "C" int insland_android_main(int argc, char** argv);

namespace
{

void make_directory(const std::string& path)
{
    if (!path.empty()) ::mkdir(path.c_str(), 0755);
}

bool write_asset(AAssetManager* manager, const char* name, const std::string& destination)
{
    AAsset* asset = AAssetManager_open(manager, name, AASSET_MODE_BUFFER);
    if (!asset) return false;

    const void* buffer = AAsset_getBuffer(asset);
    const off_t length = AAsset_getLength(asset);
    bool written = false;
    if (buffer && length > 0)
    {
        if (FILE* file = std::fopen(destination.c_str(), "wb"))
        {
            const std::size_t size = static_cast<std::size_t>(length);
            written = std::fwrite(buffer, 1, size, file) == size;
            std::fclose(file);
        }
    }
    AAsset_close(asset);
    return written;
}

void extract_assets(android_app* app)
{
    if (!app || !app->activity || !app->activity->assetManager) return;
    if (!app->activity->internalDataPath) return;

    const std::string root = app->activity->internalDataPath;
    const std::string assets = root + "/assets";
    make_directory(root);
    make_directory(assets);

    AAssetManager* manager = app->activity->assetManager;
    const char* files[] = {"model.glb", "draco_box.glb", "demo.scene.json"};
    for (const char* file : files)
    {
        if (write_asset(manager, file, assets + "/" + file))
            __android_log_print(ANDROID_LOG_INFO, "Insland", "extracted %s", file);
    }

    // Optional texture folder used by the glTF sample.
    AAssetDir* textures = AAssetManager_openDir(manager, "textures");
    if (textures)
    {
        const std::string directory = assets + "/textures";
        make_directory(directory);
        const char* name = nullptr;
        while ((name = AAssetDir_getNextFileName(textures)) != nullptr)
            write_asset(manager, (std::string("textures/") + name).c_str(), directory + "/" + name);
        AAssetDir_close(textures);
    }

    insland::platform::set_android_asset_root(assets);
}

void on_app_command(android_app* app, int32_t command)
{
    switch (command)
    {
    case APP_CMD_INIT_WINDOW:
        insland::platform::set_android_native_window(app->window);
        break;
    case APP_CMD_TERM_WINDOW:
        insland::platform::set_android_native_window(nullptr);
        break;
    default:
        break;
    }
}

} // namespace

void android_main(struct android_app* app)
{
    app->onAppCmd = on_app_command;
    insland::platform::set_android_app(app);
    extract_assets(app);

    // The window arrives asynchronously through APP_CMD_INIT_WINDOW.
    while (!app->window && !app->destroyRequested)
    {
        int events = 0;
        android_poll_source* source = nullptr;
        ALooper_pollOnce(-1, nullptr, &events, reinterpret_cast<void**>(&source));
        if (source) source->process(app, source);
    }

    if (app->destroyRequested) return;

    insland::platform::set_android_native_window(app->window);

    char* argv[] = {const_cast<char*>("InslandGame"), nullptr};
    insland_android_main(1, argv);

    ANativeActivity_finish(app->activity);
}

#endif // __ANDROID__
