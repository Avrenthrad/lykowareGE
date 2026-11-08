#pragma once
#include <lykoware/character_asset.hpp>
namespace character_fit {
struct Placement { float scale=1.F; lykoware::math::Vec3 offset{}; };
inline bool fit(const lykoware::Bounds& bounds,float height,Placement& out,std::string& error)
{
    lykoware::character::Alignment aligned;
    if(!lykoware::character::align(bounds,height,0,aligned,error)){return false;}
    out={aligned.scale,aligned.offset};return true;
}
}
