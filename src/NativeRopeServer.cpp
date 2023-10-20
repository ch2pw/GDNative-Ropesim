#include "NativeRopeServer.hpp"
#include <PoolArrays.hpp>
#include <Vector2.hpp>
#include <Curve.hpp>
#include <algorithm>
#include <OS.hpp>
#include <Engine.hpp>
#include <World2D.hpp>
#include <Physics2DDirectSpaceState.hpp>

using namespace godot;

float get_point_perc(int index, const PoolVector2Array& points)
{
    return index / (points.size() > 0 ? float(points.size() - 1) : 0.f);
}

Vector2 damp_vec(Vector2 value, float damping_factor, float delta)
{
    return value.linear_interpolate(value.ZERO, 1.0 - exp(-damping_factor * delta));
}


void NativeRopeServer::_register_methods()
{
    register_method("_physics_process", &NativeRopeServer::_physics_process);
    register_method("register_rope", &NativeRopeServer::register_rope);
    register_method("unregister_rope", &NativeRopeServer::unregister_rope);
    register_method("get_num_ropes", &NativeRopeServer::get_num_ropes);
    register_method("get_computation_time", &NativeRopeServer::get_computation_time);
    register_method("set_update_in_editor", &NativeRopeServer::set_update_in_editor);
    register_method("get_update_in_editor", &NativeRopeServer::get_update_in_editor);
    register_property<NativeRopeServer, bool>("update_in_editor", &NativeRopeServer::set_update_in_editor, &NativeRopeServer::get_update_in_editor, false);
    register_signal<NativeRopeServer>((char*)"on_post_update");
    register_signal<NativeRopeServer>((char*)"on_pre_update");
}

NativeRopeServer::NativeRopeServer()
{
}

NativeRopeServer::~NativeRopeServer()
{
    // add your cleanup here
}

void NativeRopeServer::_init()
{
    _last_time = 0.0;
    _update_in_editor = false;
}

void NativeRopeServer::_enter_tree()
{
    _start_stop_process();
}

void NativeRopeServer::_physics_process(float delta)
{
    emit_signal("on_pre_update");
    auto start = OS::get_singleton()->get_ticks_usec();

    for (Node2D* rope : _ropes)
        _simulate(rope, delta);

    _last_time = (OS::get_singleton()->get_ticks_usec() - start) / 1000.f;
    emit_signal("on_post_update");
}

void NativeRopeServer::register_rope(Node2D* rope)
{
    _ropes.emplace_back(rope);
    _start_stop_process();
    // Godot::print("Rope registered: " + String::num_int64(_ropes.size()));
}

void NativeRopeServer::unregister_rope(Node2D* rope)
{
    if (_ropes.empty() || rope != _ropes.back())
    {
        auto it = std::find(_ropes.begin(), _ropes.end(), rope);
        if (it == _ropes.end())
        {
            WARN_PRINT("Unregistering non-registered Rope");
            return;
        }

        // Swap and pop
        (*it) = _ropes.back();
    }

    _ropes.pop_back();
    _start_stop_process();
    // Godot::print("Rope unregistered: " + String::num_int64(_ropes.size()));
}

void NativeRopeServer::set_update_in_editor(bool value)
{
    _update_in_editor = value;
    _start_stop_process();
}

bool NativeRopeServer::get_update_in_editor() const
{
    return _update_in_editor;
}

void NativeRopeServer::_start_stop_process()
{
    _last_time = 0.f;
    set_physics_process(!_ropes.empty()
            && (!Engine::get_singleton()->is_editor_hint() || get_update_in_editor()));

    // if (is_physics_processing())
    //     Godot::print("RopeServer deactivated");
    // else
    //     Godot::print("RopeServer activated");
}

void NativeRopeServer::_simulate(Node2D* rope, float delta)
{
    PoolVector2Array points = rope->call("get_points");
    if (points.size() < 2)
        return;

    PoolVector2Array oldpoints = rope->call("get_old_points");
    Ref<Curve> damping_curve = rope->get("damping_curve");
    float gravity = rope->get("gravity");
    float damping = rope->get("damping");
    float stiffness = rope->get("stiffness");
    int num_constraint_iterations = rope->get("num_constraint_iterations");
    PoolRealArray seg_lengths = rope->call("get_segment_lengths");
    Vector2 parent_seg_dir = rope->get_global_transform().basis_xform(Vector2::DOWN).normalized();
    Vector2 last_stiffness_force;

    // Simulate
    for (size_t i = 1; i < points.size(); ++i)
    {
        Vector2 vel = points[i] - oldpoints[i];
        float dampmult = damping_curve.is_valid() ? damping_curve->interpolate_baked(get_point_perc(i, points)) : 1.0;

        if (stiffness > 0)
        {
            //  |  parent_seg_dir     --->  parent_seg_tangent
            //  |                     \
            //  V                      \   seg_dir
            //  \  seg_dir              V
            //   \
            //    V
            Vector2 seg_dir = (points[i] - points[i - 1]).normalized();
            Vector2 parent_seg_tangent = parent_seg_dir.tangent();
            float angle = seg_dir.angle_to(parent_seg_dir);

            // The force directs orthogonal to the current segment
            // TODO: Ask a physicist if this is physically correct.
            Vector2 force_dir = seg_dir.tangent();

            // Scale the force the further the segment bends.
            // angle is signed and can be used to determine the force direction
            // TODO: Ask a physicist if this is physically correct.
            last_stiffness_force += force_dir * (-angle / 3.1415) * stiffness;
            vel += last_stiffness_force;
            parent_seg_dir = seg_dir;
        }

        oldpoints.set(i, points[i]);
        points.set(i, points[i] + damp_vec(vel, damping * dampmult, delta) + Vector2(0, gravity * delta));
    }

    // Constraint
    for (int _ = 0; _ < num_constraint_iterations; ++_)
    {
        points.set(0, rope->get_global_position());
        points.set(1, points[0] + (points[1] - points[0]).normalized() * seg_lengths[0]);

        for (size_t i = 1; i < points.size() - 1; ++i)
        {
            Vector2 diff = points[i + 1] - points[i];
            float distance = diff.length();
            Vector2 dir = diff / distance;
            float error = (seg_lengths[i] - distance) * 0.5;
            points.set(i, points[i] - error * dir);
            points.set(i + 1, points[i + 1] + error * dir);
        }
    }

    // Collisions
    if (rope->get("enable_collisions"))
    {
        Physics2DDirectSpaceState* space = rope->get_world_2d()->get_direct_space_state();
        const int mask = rope->get("collision_mask");
        const int max_slides = rope->get("max_num_slides");

        for (size_t i = 1; i < points.size(); ++i)
        {
            Vector2 start = oldpoints[i];
            Vector2 end = points[i];
            Vector2 vel = end - start;
            const Vector2 original_vel = vel;

            if (vel.length_squared() == 0.0)
                break;

            for (int slide = 0; slide < max_slides; ++slide)
            {
                const Dictionary result = space->intersect_ray(start, end, Array(), mask);

                if (result.empty()) {
                    points.set(i, end);
                    break;
                }

                const Vector2 position = result["position"];
                const Vector2 normal = result["normal"];
                const float traveled = start.distance_to(position) / vel.length();

                // If stuck, do nothing and keep the simulated position so it can unstuck itself.
                if (traveled <= 0.001)
                    break;

                points.set(i, position + normal);

                if (traveled > 0.999)
                    break;

                vel = normal.slide(vel) * (1.0 - traveled);

                // Stop if the new velocity goes against the initial direction. Prevents jitter.
                if (vel.dot(original_vel) < 0.0)
                    break;

                start = points[i];
                end = start + vel;
            }
        }
    }

    rope->call("set_points", points);
    rope->call("set_old_points", oldpoints);
}

float NativeRopeServer::get_computation_time() const
{
    return _last_time;
}

int NativeRopeServer::get_num_ropes() const
{
    return _ropes.size();
}
