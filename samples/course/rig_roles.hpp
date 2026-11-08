#pragma once
#include <lykoware/character_asset.hpp>
namespace rig_roles {
inline constexpr auto names = lykoware::character::role_names;
using Names = lykoware::character::RoleNames;
using Indices = lykoware::character::Roles;
inline bool resolve(const lykoware::animation::Skeleton& skeleton,const Names& requested,Indices& out,std::string& error)
{ return lykoware::character::resolve_roles(skeleton,requested,out,error); }
}
