#include "character_command.hpp"
#include <lykoware/character_asset.hpp>
#include <nlohmann/json.hpp>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
namespace lykoware::cli {
namespace {
using Json=nlohmann::json;
std::filesystem::path path(std::string_view s){return std::filesystem::path{std::u8string(s.begin(),s.end())};}
Json vec(lykoware::math::Vec3 p){return Json::array({p.x,p.y,p.z});}
Json rig(const character::RigSummary& r){return {{"valid",r.valid},{"unique_names",r.unique_names},{"unit_scales",r.unit_scales},{"nonzero_links",r.nonzero_links},{"roots",r.roots},{"issues",r.issues}};}
bool same_file(const std::filesystem::path& a,const std::filesystem::path& b){
    if(a.empty()||b.empty()){return false;}
    std::error_code ea,eb;
    const auto ca=std::filesystem::weakly_canonical(a,ea),cb=std::filesystem::weakly_canonical(b,eb);
    if(!ea&&!eb&&ca==cb){return true;}
    std::error_code ec;return std::filesystem::equivalent(a,b,ec)&&!ec;
}
}
int character_command(std::span<const std::string> args)
{
    std::filesystem::path model,profile_file,compare,output;
    character::Profile profile;std::string error;
    bool resolved_inputs=false;
    Json report{{"format","lykoware.character-inspection"},{"schema_version",1},{"ok",false}};
    try {
        for(std::size_t i=0;i<args.size();++i){
            if(i+1>=args.size()){throw std::invalid_argument("inspect-character options require values");}
            const auto& key=args[i];const auto value=path(args[++i]);
            auto* slot=key=="--model"?&model:key=="--profile"?&profile_file:key=="--compare-model"?&compare:key=="--output"?&output:nullptr;
            if(!slot||!slot->empty()){throw std::invalid_argument("Unknown or duplicate inspect-character option: "+key);}*slot=value;
        }
        if(model.empty()==profile_file.empty()){throw std::invalid_argument("inspect-character requires exactly one --model or --profile");}
        if(!profile_file.empty()){if(!character::load_profile(profile_file,profile,error)){throw std::invalid_argument(error);}model=profile.model;}else{profile.model=model;}
        resolved_inputs=true;
        if(!output.empty()){
            for(const auto& input:{model,profile_file,compare}){if(same_file(input,output)){throw std::invalid_argument("Inspection output must not replace an input file");}}
        }
        const auto asset=gltf::load_skinned(model,error);if(!asset){throw std::invalid_argument(error);}
        character::Inspection inspected;if(!character::inspect(*asset,profile,inspected,error)){throw std::invalid_argument(error);}
        const auto& a=inspected.placement;const auto& c=inspected.body;
        report["motion_authority"]=character::motion_authority;
        report["turn_degrees_per_second"]=profile.turn_degrees_per_second.value_or(character::default_turn_degrees_per_second);
        report["ok"]=true;report["conventions"]={{"up","+Y"},{"forward","-Z"},{"handedness","right"},{"units","metres"}};
        report["vertices"]=inspected.vertices;report["joints"]=asset->skeleton.joints.size();report["rig"]=rig(inspected.rig);
        report["bind_bounds"]={{"min",vec(inspected.bind_bounds.min)},{"max",vec(inspected.bind_bounds.max)}};
        report["alignment"]={{"scale",a.scale},{"offset",vec(a.offset)},{"rotation",{a.rotation.x,a.rotation.y,a.rotation.z,a.rotation.w}}};
        report["capsule"]={{"height",c.height},{"radius",c.radius},{"cylinder_half_height",c.cylinder_half_height},{"eye_height",c.eye_height}};
        report["warnings"]=inspected.warnings;report["clips"]=Json::array();
        for(const auto& clip:inspected.clips){report["clips"].push_back({{"name",clip.name},{"seconds",clip.seconds},{"translated_root",clip.translated_root}});}
        if(inspected.roles){report["roles"]=*inspected.roles;}
        if(!compare.empty()){
            const auto other=gltf::load_skinned(compare,error);if(!other){throw std::invalid_argument(error);}
            const auto match=character::compare_rigs(asset->skeleton,other->skeleton);
            report["compatibility"]={{"compatible",match.compatible},{"issues",match.issues}};
        }
    }catch(const std::exception& e){report["ok"]=false;report["error"]=e.what();}
    const auto text=report.dump(2)+"\n";
    if(output.empty()||!resolved_inputs){std::cout<<text;}else{
        // A rejected source/output collision must not open the source even for an error report.
        for(const auto& input:{model,profile_file,compare}){if(same_file(input,output)){std::cout<<text;return 2;}}
        std::ofstream file(output,std::ios::binary);file<<text;file.close();if(!file){throw std::runtime_error("Cannot write character inspection");}
    }
    return report["ok"].get<bool>()?0:2;
}
}
