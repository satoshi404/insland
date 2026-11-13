#pragma once

// ---------------------------------------------------------------------------
// Insland Engine - raymarched sky and volumetric clouds
// ---------------------------------------------------------------------------
// A fullscreen pass that reconstructs the view ray from the inverse
// view-projection matrix and marches it through a cloud layer.  It is drawn
// right after the frame is cleared and before any geometry, with depth testing
// disabled, so it acts as an infinite background.
//
//     Sky sky;
//     sky.create(renderer);
//     sky.draw(renderer, camera, engine.time());
//
// The shader is written in HLSL and converted by Diligent for OpenGL, so it
// avoids C-style casts and backend-specific intrinsics.
// ---------------------------------------------------------------------------

#include <cstdint>

#include "RefCntAutoPtr.hpp"
#include "RenderDevice.h"
#include "DeviceContext.h"
#include "PipelineState.h"
#include "ShaderResourceBinding.h"
#include "Buffer.h"

#include "core/types.hpp"

namespace insland
{

class Camera;
namespace platform { class Renderer; }

class Sky
{
public:
    Sky() = default;

    bool create(platform::Renderer& renderer);
    void shutdown();

    bool valid() const { return pso_ != nullptr; }

    /// Draws the sky.  `time` animates the clouds.
    void draw(platform::Renderer& renderer, const Camera& camera, double time);

    void set_sun(const Vec3& direction, float intensity = 1.0f)
    {
        sun_direction_ = glm::normalize(direction);
        sun_intensity_ = intensity;
    }

private:
    Diligent::RefCntAutoPtr<Diligent::IPipelineState> pso_;
    Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding> srb_;
    Diligent::RefCntAutoPtr<Diligent::IBuffer> constant_buffer_;
    Diligent::IShaderResourceVariable* cb_variable_ = nullptr;

    Vec3 sun_direction_{0.32f, 0.62f, 0.72f};
    float sun_intensity_ = 1.0f;
};

} // namespace insland
