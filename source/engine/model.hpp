#pragma once

#include <cstddef>
#include <memory>
#include <string>

#include "core/types.hpp"

namespace insland
{

namespace detail
{
struct ModelData;
}

// A Model is a cheap, copyable handle to GPU resources.  Copies share the same
// underlying data, so passing models around by value is fine.
class Model
{
public:
    Model() = default;
    ~Model() = default;

    Model(const Model&) = default;
    Model& operator=(const Model&) = default;
    Model(Model&&) noexcept = default;
    Model& operator=(Model&&) noexcept = default;

    bool valid() const { return static_cast<bool>(data_); }

    const std::string& name() const;
    std::size_t mesh_count() const;
    std::size_t animation_count() const;
    std::string animation_name(std::size_t index) const;
    Bounds bounds() const;

    // Animation control -----------------------------------------------------
    void play(std::size_t animation_index = 0, bool loop = true);
    bool play(const std::string& animation_name, bool loop = true);
    void stop();
    void update(float delta_time);

    void set_animation_time(float time);
    float animation_time() const;
    void set_animation_speed(float speed);
    float animation_speed() const;
    bool is_playing() const;
    int current_animation() const;

    const detail::ModelData* data() const { return data_.get(); }
    detail::ModelData* data() { return data_.get(); }

    // Internal: engine code creates handles from its private ModelData.  The
    // constructor is public so Engine::Impl can use it; game code should only
    // obtain models from Engine::load_model()/create_cube().
    explicit Model(std::shared_ptr<detail::ModelData> data) : data_(std::move(data)) {}

private:
    std::shared_ptr<detail::ModelData> data_;
};

} // namespace insland
