#pragma once
#include <lykoware/gltf.hpp>
#include <lykoware/ik.hpp>
#include <lykoware/locomotion.hpp>
#include <array>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// ADR 0063. Asset preparation/inspection only: no simulation or renderer ownership.
// Transactional output on bool-returning operations; setup allocations may throw.
namespace lykoware::character {
inline constexpr std::array<const char*,7> role_names{{"pelvis","left_hip","left_knee","left_foot","right_hip","right_knee","right_foot"}};
inline constexpr std::array<const char*,6> clip_keys{{"idle_clip","walk_clip","run_clip","jump_clip","fall_clip","land_clip"}};
inline constexpr const char* motion_authority = "controller";
inline constexpr float default_turn_degrees_per_second = 720.F;
using RoleNames = std::array<std::string,7>;
using Roles = std::array<std::size_t,7>;
struct FootSettings {
    bool align_normals=false, pelvis=false, locking=false, heading=false;
    // While walking with one foot planted, drop the pelvis so that leg reaches (ADR 0070).
    // Requires pelvis and locking.
    bool single_support=false;
    // In single support, slide the pelvis sideways over the planted foot, as far as it dropped
    // and at most max_balance_shift metres (ADR 0072). Requires single_support.
    bool balance_shift=false;
    double max_slope_degrees=35, max_pelvis_drop=.15, max_plant_distance=.35, max_heading_degrees=60, max_balance_shift=.04;
};
[[nodiscard]] bool valid_foot_settings(const FootSettings& settings,std::string& error);
struct Profile {
    std::filesystem::path model;
    std::filesystem::path animation_graph;
    std::array<std::string,6> clips{};
    std::optional<float> height, yaw_degrees, capsule_height, capsule_radius, eye_height, turn_degrees_per_second;
    std::optional<RoleNames> bones;
    bool foot_ik = false;
    double foot_clearance = .03;
    FootSettings foot_settings;
    // Optional "locomotion" object: a version 1 locomotion profile without its own version
    // (ADR 0074). Absent means the caller's default motor profile.
    std::optional<locomotion::Profile> locomotion;
};
struct Alignment { float scale=1.F; math::Vec3 offset{}; math::Quat rotation{}; };
struct Capsule { float height=1.8F, radius=.3F, cylinder_half_height=.6F, eye_height=1.6F; };
[[nodiscard]] bool decode_profile(std::string_view text, const std::filesystem::path& directory, Profile& out, std::string& error);
[[nodiscard]] bool load_profile(const std::filesystem::path& file, Profile& out, std::string& error);
[[nodiscard]] bool align(const Bounds& bind_bounds, float height, float yaw_degrees, Alignment& out, std::string& error);
[[nodiscard]] bool capsule(const Profile& profile, Capsule& out, std::string& error);
[[nodiscard]] bool resolve_roles(const animation::Skeleton& skeleton, const RoleNames& names, Roles& out, std::string& error);
struct FootCalibration { std::array<animation::PreparedTwoBone,2> legs; };
[[nodiscard]] std::optional<FootCalibration> calibrate_feet(const animation::Skeleton& skeleton,const Roles& roles,std::string& error);
[[nodiscard]] std::optional<std::size_t> find_clip(const gltf::SkinnedModel& asset, std::string_view name, std::size_t fallback, std::string& error);

struct RigSummary {
    bool valid=false, unique_names=true, unit_scales=true, nonzero_links=true;
    std::size_t roots=0;
    std::vector<std::string> issues;
};
struct JointPair { std::size_t source=0, target=0; };
struct RigCompatibility {
    bool compatible=false;
    math::Quat root_basis{};
    std::vector<JointPair> joints;
    std::vector<std::string> issues;
};
[[nodiscard]] RigSummary describe_rig(const animation::Skeleton& skeleton);
// Full unique-name/topology mapping; root basis accounts for exporter root orientation.
// Rest axes must match under that basis. This diagnoses the supported subset, not a pose transfer.
[[nodiscard]] RigCompatibility compare_rigs(const animation::Skeleton& source, const animation::Skeleton& target);
struct ClipInfo { std::string name; float seconds=0; bool translated_root=false; };
struct Inspection {
    Bounds bind_bounds{};
    Alignment placement;
    Capsule body;
    RigSummary rig;
    std::vector<ClipInfo> clips;
    std::optional<Roles> roles;
    std::vector<std::string> warnings;
    std::size_t vertices=0;
};
[[nodiscard]] bool inspect(const gltf::SkinnedModel& asset, const Profile& profile, Inspection& out, std::string& error);
}
