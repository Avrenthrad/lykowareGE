"""Generate an original one-joint glTF and exercise real sandbox playback."""
import base64
import json
import pathlib
import re
import struct
import subprocess
import sys

exe, directory = sys.argv[1:]
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
profile.write_text(json.dumps(dict(version=1,model=path.name,bones={name:name for name in role_names},foot_ik=True,foot_clearance=.12)))
r=run(ik_args)
assert r.returncode==0 and 'failures=0' in r.stdout+r.stderr, r.stdout+r.stderr
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
