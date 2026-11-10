"""Headless profile/alignment/rig diagnostics; no Vulkan dependency."""
import base64,json,math,pathlib,struct,subprocess,sys,tempfile
exe=sys.argv[1]
with tempfile.TemporaryDirectory(prefix='lyko character ') as folder:
    root=pathlib.Path(folder);blob=bytearray();views=[];accessors=[]
    def add(fmt,values,component,count,kind):
        while len(blob)%4:blob.append(0)
        data=struct.pack('<'+fmt,*values);views.append(dict(buffer=0,byteOffset=len(blob),byteLength=len(data)));blob.extend(data)
        accessors.append(dict(bufferView=len(views)-1,componentType=component,count=count,type=kind));return len(accessors)-1
    pos=add('9f',[10,-4,20,14,-4,20,12,4,26],5126,3,'VEC3')
    normal=add('9f',[0,0,1]*3,5126,3,'VEC3');joint=add('12H',[0]*12,5123,3,'VEC4');weight=add('12f',[1,0,0,0]*3,5126,3,'VEC4')
    indices=add('3H',[0,1,2],5123,3,'SCALAR');times=add('2f',[0,1],5126,2,'SCALAR');values=add('6f',[0,0,0,0,.1,0],5126,2,'VEC3')
    model=dict(asset=dict(version='2.0'),scenes=[dict(nodes=[0,1])],nodes=[dict(mesh=0,skin=0),dict(name='root')],skins=[dict(joints=[1])],
        meshes=[dict(primitives=[dict(attributes=dict(POSITION=pos,NORMAL=normal,JOINTS_0=joint,WEIGHTS_0=weight),indices=indices)])],
        buffers=[dict(byteLength=len(blob),uri='data:application/octet-stream;base64,'+base64.b64encode(blob).decode())],bufferViews=views,accessors=accessors,
        animations=[dict(name='breathe',samplers=[dict(input=times,output=values)],channels=[dict(sampler=0,target=dict(node=1,path='translation'))])])
    asset=root/'héro 狼.gltf';asset.write_text(json.dumps(model));profile=root/'hero.character.json'
    def run(*args,ok=True):
        r=subprocess.run([exe,'inspect-character',*map(str,args)],text=True,encoding="utf-8",capture_output=True)
        assert (r.returncode==0)==ok,(r.returncode,r.stdout,r.stderr)
        return r
    r=run('--model',asset);data=json.loads(r.stdout);assert data['motion_authority']=='controller' and data['turn_degrees_per_second']==720
    assert data['foot_policy']==dict(align_normals=False,max_slope_degrees=35,pelvis=False,max_pelvis_drop=.15,locking=False,max_plant_distance=.35)
    assert data['ok'] and data['joints']==1 and data['clips'][0]['translated_root']
    original=asset.read_bytes();r=run('--model',asset,'--output',asset,ok=False);assert asset.read_bytes()==original and not json.loads(r.stdout)['ok']
    absent=root/'absent.gltf';run('--model',absent,'--output',absent,ok=False);assert not absent.exists()
    output=root/'inspection.json';run('--model',asset,'--output',output);assert json.loads(output.read_text())['ok']
    profile.write_text(json.dumps(dict(version=1,model=asset.name,height=2,yaw_degrees=90,capsule_height=2,capsule_radius=.3,eye_height=1.7,run_clip='breathe')))
    r=run('--profile',profile,'--compare-model',asset);data=json.loads(r.stdout)
    assert data['compatibility']['compatible'] and math.isclose(data['alignment']['scale'],.25,abs_tol=1e-7)
    assert math.isclose(data['capsule']['eye_height'],1.7,abs_tol=1e-6) and data['capsule']['height']==2
    for fields in [dict(yaw_degrees=181),dict(capsule_height=2,capsule_radius=1),dict(eye_height=2),dict(run_clip='missing'),dict(capsule_height=True)]:
        profile.write_text(json.dumps(dict(version=1,model=asset.name,**fields)))
        data=json.loads(run('--profile',profile,ok=False).stdout);assert not data['ok']
    profile.write_text('{"version":1,"version":1,"model":"hero.gltf"}')
    assert 'Duplicate character profile key' in run('--profile',profile,ok=False).stdout
    profile.write_text(' '*65537);assert '64 KiB' in run('--profile',profile,ok=False).stdout
    wrong=json.loads(json.dumps(model));wrong['nodes'][1]['name']='other';other=root/'other.gltf';other.write_text(json.dumps(wrong))
    data=json.loads(run('--model',asset,'--compare-model',other).stdout);assert not data['compatibility']['compatible'] and data['compatibility']['issues']
    # Argument errors must not touch a preselected output before inputs are resolved.
    run('--output',asset,'--bogus','x',ok=False);assert asset.read_bytes()==original
print('Headless character inspection, alignment, compatibility and source preservation pass')
