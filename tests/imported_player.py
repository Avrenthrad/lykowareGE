"""Generate an original one-joint glTF and exercise real sandbox playback."""
import base64
import json
import pathlib
import re
import struct
import subprocess
import sys

exe, directory, lyko = sys.argv[1:]
root = pathlib.Path(directory)
root.mkdir(parents=True, exist_ok=True)
blob = bytearray()
views, accessors = [], []
def add(fmt, values, component, count, kind):
    while len(blob) % 4:
        blob.append(0)
    data = struct.pack('<' + fmt, *values)
    views.append(dict(buffer=0, byteOffset=len(blob), byteLength=len(data)))
    blob.extend(data)
    accessors.append(dict(bufferView=len(views)-1, componentType=component, count=count, type=kind))
    return len(accessors)-1
p = add('9f', [-.2,0,0,.2,0,0,0,1,0], 5126, 3, 'VEC3')
n = add('9f', [0,0,1]*3, 5126, 3, 'VEC3')
j = add('12H', [0]*12, 5123, 3, 'VEC4')
w = add('12f', [1,0,0,0]*3, 5126, 3, 'VEC4')
i = add('3H', [0,1,2], 5123, 3, 'SCALAR')
t = add('2f', [0,3], 5126, 2, 'SCALAR')
v = add('6f', [0,0,0,0,.1,0], 5126, 2, 'VEC3')
model = dict(asset=dict(version='2.0'), scenes=[dict(nodes=[0,1])], nodes=[dict(mesh=0,skin=0),{}],
    skins=[dict(joints=[1])], meshes=[dict(primitives=[dict(attributes=dict(POSITION=p,NORMAL=n,JOINTS_0=j,WEIGHTS_0=w),indices=i)])],
    buffers=[dict(byteLength=len(blob),uri='data:application/octet-stream;base64,'+base64.b64encode(blob).decode())],
    bufferViews=views, accessors=accessors,
    animations=[dict(name='breathe',samplers=[dict(input=t,output=v)],channels=[dict(sampler=0,target=dict(node=1,path='translation'))])])
path = root / 'one joint.gltf'
path.write_text(json.dumps(model))
def run(args):
    return subprocess.run([exe,*args],capture_output=True,text=True,timeout=60)
base=['--window','--offscreen','--vulkan','--play','--no-audio','--frames','8','--character',str(path)]
r=run(base+['--idle-clip','breathe','--walk-clip','breathe'])
if r.returncode==3:
    print('SKIP: no usable Vulkan device');sys.exit(77)
assert r.returncode==0, r.stdout+r.stderr
assert 'imported character' in r.stdout+r.stderr
r=run(base+['--character-height','1.8'])
assert r.returncode==0 and 'visual scale 1.800000' in r.stdout+r.stderr, r.stdout+r.stderr
for height in ['0', '-1', 'nan', 'inf', '11', '1.8junk']:
    r=run(base+['--character-height',height])
    assert r.returncode!=0 and '--character-height must be' in r.stdout+r.stderr
profile = root / 'character.json'
profile.write_text(json.dumps(dict(version=1, model=path.name, idle_clip='breathe', walk_clip='breathe', height=1.8)))
profile_args=['--window','--offscreen','--vulkan','--play','--no-audio','--frames','8','--character-profile',str(profile)]
r=run(profile_args)
assert r.returncode==0 and 'visual scale 1.800000' in r.stdout+r.stderr, r.stdout+r.stderr
assert 'Locomotion diagnostics:' in r.stdout+r.stderr, r.stdout+r.stderr
graph_file=root/'animation.json'
graph_file.write_text(json.dumps(dict(version=1,states=[dict(name='idle',clip='breathe',events=[dict(name='entry',seconds=0)])])))
profile.write_text(json.dumps(dict(version=1,model=path.name,animation_graph=graph_file.name)))
r=run(profile_args)
assert r.returncode==0 and 'Locomotion diagnostics:' in r.stdout+r.stderr,r.stdout+r.stderr
graph_file.write_text(json.dumps(dict(version=1,states=[dict(name='idle',clip='missing')])))
r=run(profile_args)
assert r.returncode!=0 and 'Graph state clip is missing' in r.stdout+r.stderr,r.stdout+r.stderr
for key in ['run_clip','jump_clip','fall_clip','land_clip']:
    profile.write_text(json.dumps(dict(version=1, model=path.name, **{key: 'breathe'})))
    r=run(profile_args)
    assert r.returncode==0 and 'Locomotion diagnostics:' in r.stdout+r.stderr, r.stdout+r.stderr
    profile.write_text(json.dumps(dict(version=1, model=path.name, **{key: 'missing clip'})))
    r=run(profile_args)
    assert r.returncode!=0 and 'Unknown character clip' in r.stdout+r.stderr, r.stdout+r.stderr
    for invalid in ['', 2, False]:
        profile.write_text(json.dumps(dict(version=1, model=path.name, **{key: invalid})))
        r=run(profile_args)
        assert r.returncode!=0 and 'Profile locomotion clip names' in r.stdout+r.stderr, r.stdout+r.stderr
profile.write_text(json.dumps(dict(version=1,model=path.name)))
profile.write_text(json.dumps(dict(version=1,model=path.name,height=2,yaw_degrees=90,capsule_height=2,capsule_radius=.3,eye_height=1.7)))
r=run(profile_args)
assert r.returncode==0 and 'yaw 90.000000' in r.stdout+r.stderr and 'capsule height 2.000000' in r.stdout+r.stderr and 'eye height 1.700000' in r.stdout+r.stderr, r.stdout+r.stderr
profile.write_text(json.dumps(dict(version=1,model=path.name)))
r=run(profile_args+['--character',str(path)])
assert r.returncode!=0 and 'cannot mix' in r.stdout+r.stderr
for bad in [dict(version=2,model=path.name),dict(version=1,model=path.name,typo=True),dict(version=1,model=path.name,height='large'),dict(version=1,model=path.name,foot_ik=True),dict(version=1,model=path.name,foot_ik='yes')]:
    profile.write_text(json.dumps(bad))
    r=run(profile_args)
    assert r.returncode!=0, r.stdout+r.stderr
profile.write_text('{"version":1,"version":1,"model":"one joint.gltf"}')
r=run(profile_args)
assert r.returncode!=0 and 'Duplicate character profile key' in r.stdout+r.stderr
profile.write_text(' '*65537)
r=run(profile_args)
assert r.returncode!=0 and 'exceeds 64 KiB' in r.stdout+r.stderr
r=run(base+['--idle-clip','missing'])
assert r.returncode!=0 and 'Unknown character clip' in r.stdout+r.stderr, r.stdout+r.stderr
role_names=['pelvis','left_hip','left_knee','left_foot','right_hip','right_knee','right_foot']
model['nodes']=[dict(mesh=0,skin=0),dict(name='pelvis',children=[2,5]),dict(name='left_hip',children=[3]),
                dict(name='left_knee',children=[4]),dict(name='left_foot'),dict(name='right_hip',children=[6]),
                dict(name='right_knee',children=[7]),dict(name='right_foot')]
model['skins']=[dict(joints=list(range(1,8)))]
path.write_text(json.dumps(model))
profile.write_text(json.dumps(dict(version=1,model=path.name,bones={name:name for name in role_names})))
r=run(profile_args)
assert r.returncode==0 and 'Rig role right_foot = right_foot [6]' in r.stdout+r.stderr, r.stdout+r.stderr
profile.write_text(json.dumps(dict(version=1,model=path.name,bones={name:name for name in role_names},foot_ik=True)))
r=run(profile_args)
assert r.returncode!=0 and 'Foot IK calibration' in r.stdout+r.stderr, r.stdout+r.stderr
translations=[(0,1.6,0),(-.2,0,0),(0,-.8,0),(0,-.8,0),(.2,0,0),(0,-.8,0),(0,-.8,0)]
for node,translation in zip(model['nodes'][1:],translations):
    node['translation']=translation
inverse=[]
for x,y,z in [(0,1.6,0),(-.2,1.6,0),(-.2,.8,0),(-.2,0,0),(.2,1.6,0),(.2,.8,0),(.2,0,0)]:
    inverse.extend([1,0,0,0,0,1,0,0,0,0,1,0,-x,-y,-z,1])
ibm=add('112f',inverse,5126,7,'MAT4')
model['skins'][0]['inverseBindMatrices']=ibm
breath=add('6f',[0,1.6,0,0,1.62,0],5126,2,'VEC3')
model['animations'][0]['samplers'][0]['output']=breath
model['buffers']=[dict(byteLength=len(blob),uri='data:application/octet-stream;base64,'+base64.b64encode(blob).decode())]
path.write_text(json.dumps(model))
ik_args=profile_args.copy()
ik_args[ik_args.index('--frames')+1]='60'
r=run(ik_args)
assert r.returncode==0, r.stdout+r.stderr
text=r.stdout+r.stderr
contacts=re.search(r'Foot IK diagnostics:.*contacts=(\d+)',text)
assert contacts and int(contacts.group(1))>0 and 'failures=0' in text, text
applied=re.search(r'applied_frames=(\d+)',text)
assert applied and int(applied.group(1))>0, text
profile.write_text(json.dumps(dict(version=1,model=path.name,bones={name:name for name in role_names},foot_ik=True,yaw_degrees=90)))
r=run(ik_args)
rotated=r.stdout+r.stderr
assert r.returncode==0 and 'failures=0' in rotated and re.search(r'contacts=[1-9]\d*',rotated) and re.search(r'applied_frames=[1-9]\d*',rotated), rotated
profile.write_text(json.dumps(dict(version=1,model=path.name,bones={name:name for name in role_names},foot_ik=True,foot_clearance=.12)))
r=run(ik_args)
assert r.returncode==0 and 'failures=0' in r.stdout+r.stderr, r.stdout+r.stderr
profile.write_text(json.dumps(dict(version=1,model=path.name,bones={name:name for name in role_names},foot_ik=True,foot_align_normals=True,foot_max_slope_degrees=40,foot_pelvis=True,foot_max_pelvis_drop=.12)))
r=run(ik_args);stance=r.stdout+r.stderr
assert r.returncode==0 and 'failures=0' in stance and re.search(r'oriented=[1-9]\d*',stance) and re.search(r'applied_frames=[1-9]\d*',stance),stance
plant_graph=root/'plants.graph.json'
markers=[]
for half in range(15):
    names=['left_plant','right_lift'] if half%2==0 else ['left_lift','right_plant']
    markers.extend(dict(name=name,seconds=half*.2) for name in names)
plant_definition=dict(version=1,states=[dict(name='gait',clip='breathe',fade=0,events=markers)])
plant_graph.write_text(json.dumps(plant_definition))
profile.write_text(json.dumps(dict(version=1,model=path.name,bones={name:name for name in role_names},foot_ik=True,animation_graph=plant_graph.name,foot_align_normals=True,foot_pelvis=True,foot_locking=True,foot_max_plant_distance=.35)))
input_file=root/'walking.input.json';input_file.write_text(json.dumps(dict(version=1,frames=[dict(repeat=30),dict(forward=.1,repeat=90)])))
walking_args=ik_args.copy();frame_index=walking_args.index('--frames');del walking_args[frame_index:frame_index+2];walking_args+=['--play-input',str(input_file)]
def walking_run(use_cli=False):
    result=subprocess.run([lyko,"run","--",*walking_args],capture_output=True,text=True,timeout=60) if use_cli else run(walking_args);text=result.stdout+result.stderr
    assert result.returncode==0 and 'failures=0' in text,text
    counts=tuple(int(re.search(key+r'=(\d+)',text).group(1)) for key in ['planted','released','applied_frames'])
    assert min(counts)>0 and re.search(r'walk=[1-9]\d*',text),text
    return counts
assert walking_run()==walking_run(True),'fixed-clock offscreen walking replay must reproduce plant/lift counters'
# Heading lock (ADR 0068): the same walk holds each planted foot's heading.
profile.write_text(json.dumps(dict(version=1,model=path.name,bones={name:name for name in role_names},foot_ik=True,animation_graph=plant_graph.name,foot_align_normals=True,foot_pelvis=True,foot_locking=True,foot_max_plant_distance=.35,foot_heading_lock=True,foot_max_heading_degrees=45,foot_pelvis_single_support=True)))
r=run(walking_args);headed_text=r.stdout+r.stderr
assert r.returncode==0 and 'failures=0' in headed_text and re.search(r'headed=[1-9]\d*',headed_text) and re.search(r'planted=[1-9]\d*',headed_text),headed_text
# Drawn supports (ADR 0069): the falling crates' drawn poses are offered to the feet every frame.
assert re.search(r' drawn=\d+',headed_text) and re.search(r'drawn_offered=[1-9]\d*',headed_text),headed_text
# Single support (ADR 0070) runs in the same walk on flat ground and reports its counts.
assert re.search(r'single_support=\d+ swing_held=\d+',headed_text),headed_text
profile.write_text(json.dumps(dict(version=1,model=path.name,bones={name:name for name in role_names},foot_ik=True,animation_graph=plant_graph.name,foot_align_normals=True,foot_pelvis=True,foot_locking=True,foot_max_plant_distance=.35)))
r=run(walking_args+['--frames','1']);assert r.returncode!=0 and 'must match expanded play replay' in r.stdout+r.stderr,r.stdout+r.stderr
plant_definition['states'][0]['events']=[e for e in markers if e['name']!='right_lift'];plant_graph.write_text(json.dumps(plant_definition))
r=run(walking_args);assert r.returncode!=0 and 'plant and lift markers' in r.stdout+r.stderr,r.stdout+r.stderr
plant_graph.write_text(json.dumps(dict(version=1,states=[dict(name='gait',clip='breathe',events=markers)])))
for extra in [dict(foot_align_normals=True,foot_max_slope_degrees=61),dict(foot_pelvis=True,foot_max_pelvis_drop=.31),dict(foot_max_slope_degrees=20),dict(foot_max_pelvis_drop=.1),dict(foot_align_normals='yes'),dict(foot_locking=True),dict(foot_max_plant_distance=.2),dict(foot_locking=True,animation_graph=plant_graph.name,foot_max_plant_distance=2),dict(foot_heading_lock=True),dict(foot_locking=True,animation_graph=plant_graph.name,foot_heading_lock=True,foot_max_heading_degrees=91),dict(foot_locking=True,animation_graph=plant_graph.name,foot_max_heading_degrees=30),dict(foot_pelvis=True,foot_pelvis_single_support=True),dict(foot_locking=True,animation_graph=plant_graph.name,foot_pelvis_single_support=True),dict(foot_pelvis=True,foot_locking=True,animation_graph=plant_graph.name,foot_pelvis_single_support='yes')]:
    profile.write_text(json.dumps(dict(version=1,model=path.name,bones={name:name for name in role_names},foot_ik=True,**extra)))
    r=run(ik_args);assert r.returncode!=0,r.stdout+r.stderr
profile.write_text(json.dumps(dict(version=1,model=path.name,bones={name:name for name in role_names},foot_ik=True,foot_clearance=-1)))
r=run(ik_args)
assert r.returncode!=0 and 'foot_clearance must' in r.stdout+r.stderr
profile.write_text(json.dumps(dict(version=1,model=path.name,bones={name:name for name in role_names})))
model['nodes'][3]['children']=[]
model['nodes'][1]['children'].append(4)
path.write_text(json.dumps(model))
r=run(profile_args)
assert r.returncode!=0 and 'ancestry' in r.stdout+r.stderr, r.stdout+r.stderr
model['animations']=[]
path.write_text(json.dumps(model))
r=run(base)
assert r.returncode!=0 and 'requires animation clips' in r.stdout+r.stderr
r=run(['--window','--dummy','--frames','1','--character',str(path)])
assert r.returncode!=0 and '--character requires --play' in r.stdout+r.stderr
print('Imported character playback and refusals pass')
