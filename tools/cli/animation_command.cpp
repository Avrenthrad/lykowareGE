#include "animation_command.hpp"
#include <lykoware/animation_graph.hpp>
#include <lykoware/gltf.hpp>
#include <lykoware/retarget.hpp>
#include <nlohmann/json.hpp>
#include <fstream>
#include <iostream>
#include <set>
#include <stdexcept>
namespace lykoware::cli {
namespace {
using Json=nlohmann::json;
std::filesystem::path path(const std::string& s){return std::filesystem::path{std::u8string(s.begin(),s.end())};}
Json read(const std::filesystem::path& file){
    std::ifstream input(file,std::ios::binary);if(!input){throw std::invalid_argument("Cannot open animation replay");}
    std::string text(65537,'\0');input.read(text.data(),static_cast<std::streamsize>(text.size()));text.resize(static_cast<std::size_t>(input.gcount()));if(text.size()>65536){throw std::invalid_argument("Replay exceeds 64 KiB");}
    std::array<std::set<std::string>,17> keys;
    return Json::parse(text,[&](int depth,Json::parse_event_t event,Json& value){
        if(depth<0||depth>=16){throw std::invalid_argument("Replay exceeds nesting limit");}
        if(event==Json::parse_event_t::object_start){keys[static_cast<std::size_t>(depth)].clear();}
        if(event==Json::parse_event_t::key&&!keys[static_cast<std::size_t>(depth-1)].insert(value.get<std::string>()).second){throw std::invalid_argument("Duplicate replay key");}return true;
    });
}
Json pose_json(std::span<const animation::JointPose> pose){Json out=Json::array();for(const auto& p:pose){out.push_back({{"translation",{p.translation.x,p.translation.y,p.translation.z}},{"rotation",{p.rotation.x,p.rotation.y,p.rotation.z,p.rotation.w}},{"scale",{p.scale.x,p.scale.y,p.scale.z}}});}return out;}
void fields(const Json& j,std::initializer_list<std::string_view> names){if(!j.is_object()){throw std::invalid_argument("Replay requires objects");}for(const auto& [key,value]:j.items()){(void)value;if(std::find(names.begin(),names.end(),key)==names.end()){throw std::invalid_argument("Unknown replay field: "+key);}}}
}
int animation_command(std::span<const std::string> args)
{
    Json report{{"format","lykoware.animation-replay"},{"schema_version",1},{"ok",false}};
    try{
        std::filesystem::path model,graph_file,inputs,target_model;
        for(std::size_t i=0;i<args.size();++i){const auto& key=args[i];auto* slot=key=="--model"?&model:key=="--graph"?&graph_file:key=="--inputs"?&inputs:key=="--target-model"?&target_model:nullptr;
            if(!slot||!slot->empty()||i+1>=args.size()){throw std::invalid_argument("Unknown/duplicate/missing animate-character option");}*slot=path(args[++i]);}
        if(model.empty()||graph_file.empty()||inputs.empty()){throw std::invalid_argument("animate-character requires --model, --graph and --inputs");}
        std::string error;const auto asset=gltf::load_skinned(model,error);if(!asset){throw std::invalid_argument(error);}
        animation::Graph graph;if(!animation::load_graph(graph_file,graph,error)){throw std::invalid_argument(error);}
        auto player=animation::GraphPlayer::create(asset->skeleton,asset->clips,graph,error);if(!player){throw std::invalid_argument(error);}
        const auto replay=read(inputs);fields(replay,{"version","frames","root_translation_scale"});if(replay.at("version")!=1){throw std::invalid_argument("Unsupported replay version");}
        const auto& frames=replay.at("frames");if(!frames.is_array()||frames.empty()||frames.size()>256){throw std::invalid_argument("Replay requires 1..256 frames");}
        std::optional<gltf::SkinnedModel> target;std::unique_ptr<animation::Retarget> retarget;animation::Pose target_pose;std::vector<math::Mat4> target_sockets(graph.sockets.size());
        if(!target_model.empty()){target=gltf::load_skinned(target_model,error);if(!target){throw std::invalid_argument(error);}retarget=animation::Retarget::create(asset->skeleton,target->skeleton,replay.value("root_translation_scale",1.F),error);if(!retarget){throw std::invalid_argument(error);}target_pose.resize(target->skeleton.joints.size());}
        report["frames"]=Json::array();
        for(const auto& frame:frames){fields(frame,{"seconds","parameters"});std::vector<float> parameters;for(const auto& p:graph.parameters){parameters.push_back(p.initial);}
            if(frame.contains("parameters")){parameters=frame.at("parameters").get<std::vector<float>>();}
            if(!player->step(frame.at("seconds").get<float>(),parameters,error)){throw std::invalid_argument(error);}
            auto pose=player->pose();auto sockets=player->sockets();
            if(retarget){if(!retarget->apply(pose,target_pose,error)||!animation::evaluate_sockets(target->skeleton,target_pose,graph.sockets,target_sockets,error)){throw std::invalid_argument(error);}pose=target_pose;sockets=target_sockets;}
            Json result{{"state",graph.states[player->state()].name},{"age",player->age()},{"pose",pose_json(pose)},{"events",Json::array()},{"sockets",Json::object()}};
            for(const auto& event:player->events()){result["events"].push_back({{"name",graph.states[event.state].events[event.event].name},{"seconds",event.seconds}});}
            for(std::size_t i=0;i<sockets.size();++i){Json m=Json::array();for(const auto& column:sockets[i].m){for(float v:column){m.push_back(v);}}result["sockets"][graph.sockets[i].name]=std::move(m);}
            report["frames"].push_back(std::move(result));
        }report["ok"]=true;
    }catch(const std::exception& e){report.erase("frames");report["error"]=e.what();}
    std::cout<<report.dump(2)<<'\n';return report["ok"].get<bool>()?0:2;
}
}
