#include <lykoware/character_asset.hpp>
#include <lykoware/animation_graph.hpp>
#include <lykoware/ik.hpp>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <numbers>
#include <set>
#include <stdexcept>
#include <unordered_map>
#include <utility>

namespace lykoware::character {
namespace {
using Json=nlohmann::json;
bool fail(std::string& error,const std::string& text){error=text;return false;}
std::filesystem::path utf8(std::string_view s){return std::filesystem::path{std::u8string(s.begin(),s.end())};}
double norm(math::Vec3 v){return std::hypot(v.x,v.y,v.z);}
std::vector<math::Quat> orientations(const animation::Skeleton& skeleton){
    std::vector<math::Quat> out(skeleton.joints.size());
    for(std::size_t i=0;i<out.size();++i){const auto p=skeleton.joints[i].parent;out[i]=math::normalize(p?out[*p]*skeleton.joints[i].rest.rotation:skeleton.joints[i].rest.rotation);}
    return out;
}
}
bool valid_foot_settings(const FootSettings& settings,std::string& error)
{
    if(!std::isfinite(settings.max_slope_degrees)||settings.max_slope_degrees<0||settings.max_slope_degrees>60
        ||!std::isfinite(settings.max_pelvis_drop)||settings.max_pelvis_drop<0||settings.max_pelvis_drop>.3||!std::isfinite(settings.max_plant_distance)||settings.max_plant_distance<.05||settings.max_plant_distance>1
        ||!std::isfinite(settings.max_heading_degrees)||settings.max_heading_degrees<0||settings.max_heading_degrees>90){return fail(error,"Invalid foot slope/pelvis/plant/heading bounds");}
    if(settings.heading&&!settings.locking){return fail(error,"foot_heading_lock requires foot_locking");}
    if(settings.single_support&&(!settings.pelvis||!settings.locking)){return fail(error,"foot_pelvis_single_support requires foot_pelvis and foot_locking");}
    if(!std::isfinite(settings.max_balance_shift)||settings.max_balance_shift<0||settings.max_balance_shift>.1){return fail(error,"Invalid foot balance shift bound");}
    if(settings.balance_shift&&!settings.single_support){return fail(error,"foot_balance_shift requires foot_pelvis_single_support");}
    error.clear();return true;
}
bool decode_profile(std::string_view text,const std::filesystem::path& directory,Profile& out,std::string& error)
{
    if(text.size()>65536){return fail(error,"Character profile exceeds 64 KiB");}
    try {
        std::array<std::set<std::string>,9> keys;
        std::string top_key; // the root member being parsed, to name the subtree of a duplicate key
        const auto j=Json::parse(text,[&](int depth,Json::parse_event_t event,Json& value){
            if(depth>8){throw std::invalid_argument("Character profile nesting exceeds limit");}
            if(event==Json::parse_event_t::object_start){keys[static_cast<std::size_t>(depth)].clear();}
            if(event==Json::parse_event_t::key&&depth==1){top_key=value.get<std::string>();}
            if(event==Json::parse_event_t::key&&!keys[static_cast<std::size_t>(depth-1)].insert(value.get<std::string>()).second){
                throw std::invalid_argument(depth==1?"Duplicate character profile key":(top_key=="locomotion"?"Character profile locomotion: Duplicate locomotion profile key":"Duplicate character bone key"));
            }
            return true;
        });
        if(!j.is_object()||!j.contains("version")||!j["version"].is_number_integer()||j["version"]!=1
            ||!j.contains("model")||!j["model"].is_string()){return fail(error,"Character profile requires version 1 and model string");}
        const std::set<std::string> allowed{"version","model","idle_clip","walk_clip","run_clip","jump_clip","fall_clip","land_clip",
            "animation_graph","motion_authority","turn_degrees_per_second","height","yaw_degrees","capsule_height","capsule_radius","eye_height","bones","foot_ik","foot_clearance","foot_align_normals","foot_max_slope_degrees","foot_pelvis","foot_max_pelvis_drop","foot_locking","foot_max_plant_distance","foot_heading_lock","foot_max_heading_degrees","foot_pelvis_single_support","foot_balance_shift","foot_max_balance_shift","locomotion","walk_clip_speed","run_clip_speed"};
        for(const auto& item:j.items()){if(!allowed.contains(item.key())){return fail(error,"Unknown character profile key: "+item.key());}}
        if(j.contains("motion_authority")&&(!j["motion_authority"].is_string()||j["motion_authority"]!=motion_authority)){
            return fail(error,"Only controller motion authority is currently supported; root tracks are visual-only");
        }
        Profile result;
        const auto model=j["model"].get<std::string>();
        if(model.empty()||model.find('\0')!=std::string::npos){return fail(error,"Invalid profile model path");}
        result.model=utf8(model);if(result.model.is_relative()){result.model=directory/result.model;}
        if(j.contains("animation_graph")){
            if(!j["animation_graph"].is_string()){return fail(error,"animation_graph must be a path string");}
            const auto graph=j["animation_graph"].get<std::string>();
            if(graph.empty()||graph.find('\0')!=std::string::npos){return fail(error,"Invalid animation graph path");}
            result.animation_graph=utf8(graph);if(result.animation_graph.is_relative()){result.animation_graph=directory/result.animation_graph;}
        }
        for(std::size_t i=0;i<clip_keys.size();++i){
            const auto* key=clip_keys[i];if(!j.contains(key)){continue;}
            if(!j[key].is_string()||j[key].get<std::string>().empty()){return fail(error,i<2?"Profile clip names must be nonempty strings":"Profile locomotion clip names must be nonempty strings");}
            result.clips[i]=j[key].get<std::string>();
        }
        const auto number=[&](const char* key,std::optional<float>& target,double low,double high){
            if(!j.contains(key)){return;}
            if(!j[key].is_number()){throw std::invalid_argument(std::string("Profile ")+key+" must be numeric");}
            const auto value=j[key].get<double>();
            if(!std::isfinite(value)||value<low||value>high){throw std::invalid_argument(std::string("Invalid profile ")+key);}
            target=static_cast<float>(value);
        };
        number("height",result.height,.1,10);number("yaw_degrees",result.yaw_degrees,-180,180);
        number("capsule_height",result.capsule_height,.1,10);number("capsule_radius",result.capsule_radius,.0001,5);
        number("eye_height",result.eye_height,.0001,10);number("turn_degrees_per_second",result.turn_degrees_per_second,0,3600);
        number("walk_clip_speed",result.walk_clip_speed,.05,20);number("run_clip_speed",result.run_clip_speed,.05,20);
        if(result.walk_clip_speed&&result.run_clip_speed&&!(*result.walk_clip_speed<*result.run_clip_speed)){
            return fail(error,"walk_clip_speed must be slower than run_clip_speed");
        }
        if(j.contains("bones")){
            const auto& bones=j["bones"];if(!bones.is_object()||bones.size()!=role_names.size()){return fail(error,"Profile bones requires seven leg roles");}
            RoleNames names;
            for(std::size_t i=0;i<names.size();++i){
                const auto* key=role_names[i];if(!bones.contains(key)||!bones[key].is_string()||bones[key].get<std::string>().empty()){return fail(error,"Invalid profile bone role: "+std::string(key));}
                names[i]=bones[key].get<std::string>();
            }
            result.bones=std::move(names);
        }
        if(j.contains("foot_ik")){
            if(!j["foot_ik"].is_boolean()){return fail(error,"Profile foot_ik must be boolean");}
            result.foot_ik=j["foot_ik"].get<bool>();if(result.foot_ik&&!result.bones){return fail(error,"Profile foot_ik requires bones");}
        }
        if(j.contains("foot_clearance")){
            if(!result.foot_ik||!j["foot_clearance"].is_number()){return fail(error,"foot_clearance requires foot_ik and a numeric value");}
            result.foot_clearance=j["foot_clearance"].get<double>();
            if(!std::isfinite(result.foot_clearance)||result.foot_clearance<0||result.foot_clearance>.3){return fail(error,"foot_clearance must be 0..0.3 metres");}
        }
        for(const auto& option:std::array<std::pair<const char*,bool*>,3>{{{"foot_align_normals",&result.foot_settings.align_normals},{"foot_pelvis",&result.foot_settings.pelvis},{"foot_locking",&result.foot_settings.locking}}}){
            if(j.contains(option.first)){
                if(!result.foot_ik||!j[option.first].is_boolean()){return fail(error,std::string(option.first)+" requires foot_ik and a boolean");}
                *option.second=j[option.first].get<bool>();
            }
        }
        for(const auto& option:std::array<std::pair<const char*,double*>,3>{{{"foot_max_slope_degrees",&result.foot_settings.max_slope_degrees},{"foot_max_pelvis_drop",&result.foot_settings.max_pelvis_drop},{"foot_max_plant_distance",&result.foot_settings.max_plant_distance}}}){
            if(j.contains(option.first)){
                const bool enabled=std::string_view(option.first)=="foot_max_slope_degrees"?(result.foot_settings.align_normals||result.foot_settings.pelvis||result.foot_settings.locking):(std::string_view(option.first)=="foot_max_pelvis_drop"?result.foot_settings.pelvis:result.foot_settings.locking);
                if(!enabled||!j[option.first].is_number()){return fail(error,std::string(option.first)+" requires its foot opt-in and a number");}
                *option.second=j[option.first].get<double>();
            }
        }
        if(result.foot_settings.locking&&result.animation_graph.empty()){return fail(error,"foot_locking requires animation_graph");}
        if(j.contains("foot_heading_lock")){
            if(!result.foot_settings.locking||!j["foot_heading_lock"].is_boolean()){return fail(error,"foot_heading_lock requires foot_locking and a boolean");}
            result.foot_settings.heading=j["foot_heading_lock"].get<bool>();
        }
        if(j.contains("foot_max_heading_degrees")){
            if(!result.foot_settings.heading||!j["foot_max_heading_degrees"].is_number()){return fail(error,"foot_max_heading_degrees requires foot_heading_lock and a number");}
            result.foot_settings.max_heading_degrees=j["foot_max_heading_degrees"].get<double>();
        }
        if(j.contains("foot_pelvis_single_support")){
            if(!result.foot_settings.pelvis||!result.foot_settings.locking||!j["foot_pelvis_single_support"].is_boolean()){return fail(error,"foot_pelvis_single_support requires foot_pelvis, foot_locking and a boolean");}
            result.foot_settings.single_support=j["foot_pelvis_single_support"].get<bool>();
        }
        if(j.contains("foot_balance_shift")){
            if(!result.foot_settings.single_support||!j["foot_balance_shift"].is_boolean()){return fail(error,"foot_balance_shift requires foot_pelvis_single_support and a boolean");}
            result.foot_settings.balance_shift=j["foot_balance_shift"].get<bool>();
        }
        if(j.contains("foot_max_balance_shift")){
            if(!result.foot_settings.balance_shift||!j["foot_max_balance_shift"].is_number()){return fail(error,"foot_max_balance_shift requires foot_balance_shift and a number");}
            result.foot_settings.max_balance_shift=j["foot_max_balance_shift"].get<double>();
        }
        if(j.contains("locomotion")){
            const auto locomotion_fail=[&](const std::string& text){return fail(error,"Character profile locomotion: "+text);};
            if(!j["locomotion"].is_object()){return locomotion_fail("must be an object");}
            Json body=j["locomotion"];
            if(body.contains("version")){return locomotion_fail("version belongs to the character profile");}
            body["version"]=1;
            locomotion::Profile decoded;std::string inner;
            if(!locomotion::decode_profile(body.dump(),decoded,inner)){return locomotion_fail(inner);}
            result.locomotion=std::move(decoded);
        }
        if(!valid_foot_settings(result.foot_settings,error)){return false;}
        Capsule checked;if(!capsule(result,checked,error)){return false;}
        out=std::move(result);error.clear();return true;
    }catch(const std::exception& e){return fail(error,e.what());}
}
bool load_profile(const std::filesystem::path& file,Profile& out,std::string& error)
{
    std::ifstream input(file,std::ios::binary);if(!input){return fail(error,"Cannot open character profile");}
    std::string text;char c=0;
    while(input.get(c)){if(text.size()==65536){return fail(error,"Character profile exceeds 64 KiB");}text.push_back(c);}
    if(!input.eof()){return fail(error,"Cannot read character profile");}
    return decode_profile(text,file.parent_path(),out,error);
}
bool align(const Bounds& b,float height,float yaw,Alignment& out,std::string& error)
{
    for(float v:{b.min.x,b.min.y,b.min.z,b.max.x,b.max.y,b.max.z,height,yaw}){
        if(!std::isfinite(v)){return fail(error,"Character fitting requires finite bounds and height/yaw");}
    }
    if(height<.1F||height>10.F||yaw< -180.F||yaw>180.F||b.max.x<b.min.x||b.max.z<b.min.z||!(b.max.y>b.min.y)){
        return fail(error,"Character fitting requires ordered nonzero-height bounds, 0.1..10 metres and yaw -180..180");
    }
    const double scale=height/(static_cast<double>(b.max.y)-b.min.y);
    Alignment result;result.scale=static_cast<float>(scale);result.rotation=math::from_axis_angle({0,1,0},yaw*std::numbers::pi_v<float>/180.F);
    const math::Vec3 centre{static_cast<float>((static_cast<double>(b.min.x)+b.max.x)*.5*scale),
        static_cast<float>(static_cast<double>(b.min.y)*scale),static_cast<float>((static_cast<double>(b.min.z)+b.max.z)*.5*scale)};
    result.offset=math::rotate(result.rotation,centre)*-1.F;
    if(!std::isfinite(result.scale)||result.scale<=0||!std::isfinite(result.offset.x)||!std::isfinite(result.offset.y)||!std::isfinite(result.offset.z)){
        return fail(error,"Character fitting exceeds finite float range");
    }
    out=result;error.clear();return true;
}
bool capsule(const Profile& profile,Capsule& out,std::string& error)
{
    Capsule result;
    result.height=profile.capsule_height.value_or(1.8F);
    result.radius=profile.capsule_radius.value_or(std::min(.3F,result.height*.25F));
    if(profile.capsule_height||profile.capsule_radius){result.cylinder_half_height=result.height*.5F-result.radius;}
    result.eye_height=profile.eye_height.value_or(profile.capsule_height?result.height*(8.F/9.F):1.6F);
    if(!std::isfinite(result.height)||result.height<.1F||result.height>10.F||!std::isfinite(result.radius)
        ||result.radius<=0||!(result.radius<result.height*.5F)||!std::isfinite(result.eye_height)
        ||result.eye_height<=0||!(result.eye_height<result.height)||result.cylinder_half_height<=0){
        return fail(error,"Invalid capsule/camera alignment: positive cylinder and eye inside standing height required");
    }
    out=result;error.clear();return true;
}
bool resolve_roles(const animation::Skeleton& skeleton,const RoleNames& names,Roles& out,std::string& error)
{
    if(!animation::valid_skeleton(skeleton,error)){return false;}
    Roles found{};
    for(std::size_t role=0;role<names.size();++role){
        std::optional<std::size_t> match;
        for(std::size_t i=0;i<skeleton.joints.size();++i){if(skeleton.joints[i].name==names[role]&&!names[role].empty()){
            if(match){return fail(error,"Ambiguous bone name: "+names[role]);}match=i;
        }}
        if(!match){return fail(error,"Missing bone role: "+std::string(role_names[role]));}
        if(std::find(found.begin(),found.begin()+static_cast<std::ptrdiff_t>(role),*match)!=found.begin()+static_cast<std::ptrdiff_t>(role)){return fail(error,"Bone roles must be distinct");}
        found[role]=*match;
    }
    const auto ancestor=[&](std::size_t parent,std::size_t child){auto p=skeleton.joints[child].parent;while(p){if(*p==parent){return true;}p=skeleton.joints[*p].parent;}return false;};
    for(std::size_t i:{std::size_t{1},std::size_t{4}}){if(!ancestor(found[0],found[i])||!ancestor(found[i],found[i+1])||!ancestor(found[i+1],found[i+2])){return fail(error,"Bone roles require pelvis/hip/knee/foot ancestry");}}
    out=found;error.clear();return true;
}
std::optional<FootCalibration> calibrate_feet(const animation::Skeleton& skeleton,const Roles& roles,std::string& error)
{
    if(!animation::valid_skeleton(skeleton,error)){return std::nullopt;}
    for(std::size_t i=0;i<roles.size();++i){
        if(roles[i]>=skeleton.joints.size()){fail(error,"Foot IK role outside skeleton");return std::nullopt;}
        for(std::size_t j=0;j<i;++j){if(roles[i]==roles[j]){fail(error,"Foot IK roles must be distinct");return std::nullopt;}}
    }
    const auto ancestor=[&](std::size_t a,std::size_t b){auto p=skeleton.joints[b].parent;while(p){if(*p==a){return true;}p=skeleton.joints[*p].parent;}return false;};
    if(ancestor(roles[1],roles[4])||ancestor(roles[4],roles[1])){fail(error,"Foot IK legs must be independent branches");return std::nullopt;}
    const auto rest=animation::rest_pose(skeleton);std::vector<math::Mat4> fk;animation::global_transforms(skeleton,rest,fk);
    const auto position=[](const math::Mat4& m){return math::DVec3{m.m[3][0],m.m[3][1],m.m[3][2]};};
    std::array<std::optional<animation::PreparedTwoBone>,2> plans;
    for(std::size_t side=0;side<2;++side){const auto leg=side==0?std::size_t{1}:std::size_t{4};
        if(!ancestor(roles[0],roles[leg])){fail(error,"Foot IK hip must descend from pelvis");return std::nullopt;}
        plans[side]=animation::PreparedTwoBone::prepare(skeleton,roles[leg],roles[leg+1],roles[leg+2],error);
        if(!plans[side]){return std::nullopt;}
        animation::Pose checked;animation::TwoBoneSolution solved;
        if(!animation::pose_two_bone(skeleton,rest,roles[leg],roles[leg+1],roles[leg+2],position(fk[roles[leg+2]]),
            position(fk[roles[leg+1]])+math::DVec3{0,0,-.5},checked,solved,error)){return std::nullopt;}
    }
    error.clear();return FootCalibration{{*plans[0],*plans[1]}};
}
std::optional<std::size_t> find_clip(const gltf::SkinnedModel& asset,std::string_view name,std::size_t fallback,std::string& error)
{
    if(name.empty()){if(fallback<asset.clips.size()){error.clear();return fallback;}fail(error,"Character requires animation clips");return std::nullopt;}
    std::optional<std::size_t> found;
    for(std::size_t i=0;i<asset.clips.size();++i){if(asset.clips[i].name==name){if(found){fail(error,"Ambiguous character clip: "+std::string(name));return std::nullopt;}found=i;}}
    if(!found){fail(error,"Unknown character clip: "+std::string(name));}else{error.clear();}return found;
}
RigSummary describe_rig(const animation::Skeleton& skeleton)
{
    RigSummary r;std::string error;
    if(!animation::valid_skeleton(skeleton,error)){r.issues.push_back("invalid_skeleton: "+error);return r;}
    r.valid=true;std::set<std::string> names;
    std::vector<math::Mat4> globals;animation::global_transforms(skeleton,animation::rest_pose(skeleton),globals);
    for(std::size_t index=0;index<skeleton.joints.size();++index){
        const auto& j=skeleton.joints[index];
        if(j.name.empty()||!names.insert(j.name).second){r.unique_names=false;r.issues.push_back("missing_or_duplicate_name: "+j.name);}
        if(j.rest.scale!=math::Vec3{1,1,1}){r.unit_scales=false;r.issues.push_back("nonunit_scale: "+j.name);}
        if(!j.parent){++r.roots;}else{
            const auto n=norm(j.rest.translation);
            const auto& current=globals[index];const auto& parent=globals[*j.parent];
            const math::DVec3 offset{static_cast<double>(current.m[3][0])-parent.m[3][0],static_cast<double>(current.m[3][1])-parent.m[3][1],static_cast<double>(current.m[3][2])-parent.m[3][2]};
            const auto world_length=std::hypot(offset.x,offset.y,offset.z);
            if(!(n>=1e-6&&n<=1e6&&world_length>=1e-6&&world_length<=1e6)){r.nonzero_links=false;r.issues.push_back("unsupported_link_length: "+j.name);}
        }
    }
    if(r.roots!=1){r.issues.push_back("single_root_required");}return r;
}
RigCompatibility compare_rigs(const animation::Skeleton& source,const animation::Skeleton& target)
{
    RigCompatibility r;const auto a=describe_rig(source),b=describe_rig(target);
    r.issues=a.issues;r.issues.insert(r.issues.end(),b.issues.begin(),b.issues.end());
    if(!a.valid||!b.valid||!a.unique_names||!b.unique_names||!a.unit_scales||!b.unit_scales||!a.nonzero_links||!b.nonzero_links||a.roots!=1||b.roots!=1){return r;}
    if(source.joints.size()!=target.joints.size()){r.issues.push_back("joint_count_mismatch");return r;}
    std::unordered_map<std::string,std::size_t> by_name;
    for(std::size_t i=0;i<target.joints.size();++i){by_name.emplace(target.joints[i].name,i);}
    std::vector<std::size_t> mapped(source.joints.size());
    for(std::size_t i=0;i<source.joints.size();++i){const auto it=by_name.find(source.joints[i].name);if(it==by_name.end()){r.issues.push_back("missing_target_joint: "+source.joints[i].name);return r;}mapped[i]=it->second;r.joints.push_back({i,it->second});}
    const auto sr=orientations(source),tr=orientations(target);
    std::size_t root=0;while(source.joints[root].parent){++root;}
    r.root_basis=math::normalize(tr[mapped[root]]*math::conjugate(sr[root]));
    for(const auto pair:r.joints){
        const auto& s=source.joints[pair.source];const auto& t=target.joints[pair.target];
        if(s.parent.has_value()!=t.parent.has_value()||(s.parent&&t.parent&&mapped[*s.parent]!=*t.parent)){r.issues.push_back("parent_mapping_mismatch: "+s.name);}
        const auto expected=math::normalize(r.root_basis*sr[pair.source]);const auto actual=tr[pair.target];
        const double d=static_cast<double>(expected.x)*actual.x+static_cast<double>(expected.y)*actual.y+static_cast<double>(expected.z)*actual.z+static_cast<double>(expected.w)*actual.w;
        if(std::abs(d)<1.-1e-5){r.issues.push_back("rest_axis_mismatch: "+s.name);}
        if(s.parent&&t.parent){const auto x=s.rest.translation,y=t.rest.translation;const double c=(static_cast<double>(x.x)*y.x+static_cast<double>(x.y)*y.y+static_cast<double>(x.z)*y.z)/(norm(x)*norm(y));if(c<1.-1e-5){r.issues.push_back("rest_direction_mismatch: "+s.name);}}
    }
    r.compatible=r.issues.empty();return r;
}
bool inspect(const gltf::SkinnedModel& asset,const Profile& profile,Inspection& out,std::string& error)
{
    if(!valid_foot_settings(profile.foot_settings,error)){return false;}
    if((profile.foot_settings.align_normals||profile.foot_settings.pelvis||profile.foot_settings.locking)&&!profile.foot_ik){return fail(error,"Foot orientation/pelvis settings require foot_ik");}
    if(profile.foot_settings.locking&&profile.animation_graph.empty()){return fail(error,"foot_locking requires animation_graph");}
    if(!profile.animation_graph.empty()){
        animation::Graph graph;if(!animation::load_graph(profile.animation_graph,graph,error)){return false;}
        if(!animation::GraphPlayer::create(asset.skeleton,asset.clips,graph,error)){return false;}
        if(profile.foot_settings.locking){
            std::array<bool,4> found{};constexpr std::array<const char*,4> markers{{"left_plant","left_lift","right_plant","right_lift"}};
            for(const auto& state:graph.states){for(const auto& event:state.events){for(std::size_t i=0;i<markers.size();++i){found[i]=found[i]||event.name==markers[i];}}}
            if(std::find(found.begin(),found.end(),false)!=found.end()){return fail(error,"foot_locking graph requires left/right plant and lift markers");}
        }
    }
    if(profile.turn_degrees_per_second&&(!std::isfinite(*profile.turn_degrees_per_second)||*profile.turn_degrees_per_second<0||*profile.turn_degrees_per_second>3600)){return fail(error,"Invalid turn_degrees_per_second");}
    if(!std::isfinite(profile.foot_clearance)||profile.foot_clearance<0||profile.foot_clearance>.3){return fail(error,"foot_clearance must be 0..0.3 metres");}
    if(!valid_mesh(asset.model.mesh,error)||!animation::valid_skeleton(asset.skeleton,error)){return false;}
    if(asset.clips.empty()){return fail(error,"Character requires animation clips");}
    if(asset.joints.size()!=asset.model.mesh.positions.size()||asset.weights.size()!=asset.joints.size()){return fail(error,"Character skin array sizes mismatch");}
    for(std::size_t i=0;i<asset.joints.size();++i){const auto w=asset.weights[i];const std::array<float,4> values{w.x,w.y,w.z,w.w};double sum=0;
        for(std::size_t k=0;k<4;++k){if(asset.joints[i][k]>=asset.skeleton.joints.size()||!std::isfinite(values[k])||values[k]<0){return fail(error,"Invalid character skin weights/joints");}sum+=values[k];}
        if(std::abs(sum-1.)>1e-4){return fail(error,"Character weights must sum to one");}
    }
    Inspection result;result.vertices=asset.joints.size();result.bind_bounds=bounds(asset.model.mesh);result.rig=describe_rig(asset.skeleton);
    if(!capsule(profile,result.body,error)){return false;}
    if(profile.height||profile.yaw_degrees){const float h=profile.height.value_or(result.bind_bounds.max.y-result.bind_bounds.min.y);if(!align(result.bind_bounds,h,profile.yaw_degrees.value_or(0),result.placement,error)){return false;}}
    for(const auto& clip:asset.clips){if(!animation::valid_clip(clip,asset.skeleton,error)){return false;}bool translated=false;for(const auto& channel:clip.channels){if(channel.target==animation::Target::Translation&&!asset.skeleton.joints[channel.joint].parent){translated=true;}}result.clips.push_back({clip.name,animation::duration(clip),translated});}
    for(std::size_t i=0;i<profile.clips.size();++i){if(!profile.clips[i].empty()&&!find_clip(asset,profile.clips[i],0,error)){return false;}}
    if(profile.bones){Roles roles;if(!resolve_roles(asset.skeleton,*profile.bones,roles,error)){return false;}result.roles=roles;
        if(profile.foot_ik&&!calibrate_feet(asset.skeleton,roles,error)){return fail(error,"Foot IK calibration: "+error);}
    }else if(profile.foot_ik){return fail(error,"Profile foot_ik requires bones");}
    std::vector<math::Mat4> globals;animation::global_transforms(asset.skeleton,animation::rest_pose(asset.skeleton),globals);
    for(const auto& m:globals){for(const auto& col:m.m){for(float v:col){if(!std::isfinite(v)){return fail(error,"Character rest transforms overflow");}}}}
    const float visual_height=(result.bind_bounds.max.y-result.bind_bounds.min.y)*result.placement.scale;
    if(std::abs(visual_height-result.body.height)>result.body.height*.1F){result.warnings.push_back("visual_height_differs_from_capsule");}
    if(!result.rig.issues.empty()){result.warnings.push_back("rig_outside_initial_retarget_subset");}
    for(const auto& clip:result.clips){if(clip.translated_root){result.warnings.push_back("root_translation_is_visual_only: "+clip.name);}}
    out=std::move(result);error.clear();return true;
}
}
