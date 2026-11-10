"""Original glTF replay fixture: graph, events, sockets and retarget through the installed CLI."""
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
    graph=root/'motion.json';replay=root/'inputs.json'
    definition=dict(version=1,parameters=[dict(name='speed',initial=0)],states=[dict(name='breathing',clip='breathe',fade=0,events=[dict(name='start',seconds=0),dict(name='footfall',seconds=.5)])],sockets=[dict(name='hat',joint='root',translation=[0,1,0])])
    graph.write_text(json.dumps(definition));replay.write_text(json.dumps(dict(version=1,frames=[dict(seconds=.5),dict(seconds=.5)])))
    def run(*args,ok=True):
        result=subprocess.run([exe,'animate-character','--model',str(asset),'--graph',str(graph),'--inputs',str(replay),*map(str,args)],text=True,encoding='utf-8',capture_output=True)
        assert (result.returncode==0)==ok,(result.stdout,result.stderr)
        return json.loads(result.stdout)
    result=run();assert result['ok']
    frames=result['frames'];assert [e['name'] for e in frames[0]['events']]==['start','footfall']
    assert math.isclose(frames[0]['pose'][0]['translation'][1],.05,abs_tol=1e-6)
    assert math.isclose(frames[0]['sockets']['hat'][13],1.05,abs_tol=1e-6)
    assert [e['name'] for e in frames[1]['events']]==['start']
    target=json.loads(json.dumps(model));target['nodes'][1]['rotation']=[0,0,math.sin(math.pi/4),math.cos(math.pi/4)];target['nodes'][1]['translation']=[10,0,0]
    target_asset=root/'target.gltf';target_asset.write_text(json.dumps(target))
    result=run('--target-model',target_asset);point=result['frames'][0]['pose'][0]['translation'];assert math.isclose(point[0],9.95,abs_tol=1e-5) and math.isclose(point[1],0,abs_tol=1e-5)
    assert math.isclose(result['frames'][0]['sockets']['hat'][12],8.95,abs_tol=1e-5)
    profile.write_text(json.dumps(dict(version=1,model=asset.name,animation_graph=graph.name)))
    inspected=subprocess.run([exe,'inspect-character','--profile',str(profile)],capture_output=True,text=True,encoding='utf-8');assert inspected.returncode==0,(inspected.stdout,inspected.stderr)
    graph_original=graph.read_bytes()
    source_guard=subprocess.run([exe,'inspect-character','--profile',str(profile),'--output',str(graph)],capture_output=True,text=True,encoding='utf-8');assert source_guard.returncode!=0 and graph.read_bytes()==graph_original
    original=asset.read_bytes();run('--output',asset,ok=False);assert asset.read_bytes()==original
    replay.write_text('{"version":1,"version":1,"frames":[]}');assert not run(ok=False)['ok']
    replay.write_text(json.dumps(dict(version=1,frames=[dict(seconds=.1,parameters=[1,2])])))
    assert not run(ok=False)['ok']
    graph.write_text('{"version":1,"version":1,"states":[]}');assert not run(ok=False)['ok']
print('CLI authored animation, clip intervals, sockets, retarget basis and refusals pass')
