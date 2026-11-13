#pragma once

// ---------------------------------------------------------------------------
// Insland Engine - Android platform bridge
// ---------------------------------------------------------------------------
// The native activity (android_native_app_glue) owns the ANativeWindow and the
// application lifecycle.  These functions hand that state to the engine:
//
//     insland::platform::set_android_native_window(app->window);
//     config.native_window = insland::platform::android_native_window();
//
// They are no-ops on desktop platforms; include this header only when
// __ANDROID__ is defined.
// ---------------------------------------------------------------------------

#include <string>

namespace insland::platform
{

void set_android_app(void* app);
void* android_current_app();

void set_android_native_window(void* window);
void* android_native_window();

/// Directory containing the assets extracted from the APK.
void set_android_asset_root(const std::string& root);
const std::string& android_asset_root();

} // namespace insland::platform
