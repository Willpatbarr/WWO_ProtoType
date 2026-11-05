"""Bakes the Godot build's body animations (Quaternius Universal Animation Library 1 and 2, CC0:
enemies/models/ual/UAL1_Standard.glb and UAL2_Standard.glb in the Godot project) onto the UE5
mannequin skeleton as AnimSequence assets in /Game/WWO/Anims/UAL (A_<Godot name>).

Run headless from the project folder (the Godot project path is read from WWO_GODOT, default
~/Documents/WhatWasOurs):
  UnrealEditor-Cmd WhatWasOurs.uproject -run=pythonscript -script=Tools/bake_ual_anims.py -unattended -nullrhi

The UAL skeleton uses the mannequin's bone names but rests in a T-pose with three spine bones; Manny
rests in an A-pose with five. Each frame is retargeted in mesh space:
- Every mapped bone takes the UAL bone's world rotation change from its rest pose, applied after a
  fixed per-bone alignment that turns Manny's rest bone onto the UAL rest bone (bone direction, and
  the palm or the left-right axis for twist). Manny's spine_02 / spine_04 / neck_02 take the halfway
  rotation of the UAL bones around them.
- The pelvis moves like the UAL pelvis, scaled by the pelvis heights; every other bone keeps Manny's
  own bone lengths. Bones the UAL rig doesn't have (twist, corrective, IK) follow their parents.
Sampled at 30 fps, like Godot's import. Safe to rerun: existing assets are rebuilt in place.
"""
import json, math, os, struct
# --- quaternion helpers (x,y,z,w), standard Hamilton product ---
def qmul(a,b):
    ax,ay,az,aw=a; bx,by,bz,bw=b
    return (aw*bx+ax*bw+ay*bz-az*by, aw*by-ax*bz+ay*bw+az*bx, aw*bz+ax*by-ay*bx+az*bw, aw*bw-ax*bx-ay*by-az*bz)
def qinv(q): return (-q[0],-q[1],-q[2],q[3])
def qnorm(q):
    n=math.sqrt(sum(x*x for x in q)); return tuple(x/n for x in q)
def qrot(q,v):
    x,y,z,w=q; vx,vy,vz=v
    tx=2*(y*vz-z*vy); ty=2*(z*vx-x*vz); tz=2*(x*vy-y*vx)
    return (vx+w*tx+(y*tz-z*ty), vy+w*ty+(z*tx-x*tz), vz+w*tz+(x*ty-y*tx))
def slerp(a,b,t):
    d=sum(x*y for x,y in zip(a,b))
    if d<0: b=tuple(-x for x in b); d=-d
    if d>0.9995: return qnorm(tuple(x+(y-x)*t for x,y in zip(a,b)))
    th=math.acos(d); s=math.sin(th)
    return tuple((math.sin((1-t)*th)*x+math.sin(t*th)*y)/s for x,y in zip(a,b))
def arc(u,v):
    # shortest rotation taking unit u to unit v
    d=sum(a*b for a,b in zip(u,v))
    c=(u[1]*v[2]-u[2]*v[1], u[2]*v[0]-u[0]*v[2], u[0]*v[1]-u[1]*v[0])
    if d < -0.999999: return (1.0,0.0,0.0,0.0)
    return qnorm((c[0],c[1],c[2],1.0+d))
def sub(a,b): return tuple(x-y for x,y in zip(a,b))
def add(a,b): return tuple(x+y for x,y in zip(a,b))
def mul(a,s): return tuple(x*s for x in a)
def unit(a):
    n=math.sqrt(sum(x*x for x in a)); return tuple(x/n for x in a) if n>1e-9 else a
# glTF (right-handed, y up, UAL faces +z, left +x)  ->  Manny mesh space (z up, faces +y, left +x), cm
def P(v): return (v[0]*100.0, v[2]*100.0, v[1]*100.0)
def Pq(q): return (-q[0], -q[2], -q[1], q[3])
# --- glb ---
def load(path):
    b=open(path,'rb').read(); _,_,length=struct.unpack_from('<4sII',b,0); off=12; js=None; binc=None
    while off<length:
        clen,ctype=struct.unpack_from('<I4s',b,off); off+=8; chunk=b[off:off+clen]; off+=clen
        if ctype==b'JSON': js=json.loads(chunk)
        elif ctype==b'BIN\x00': binc=chunk
    return js,binc
def accessor(js,binc,i):
    a=js['accessors'][i]; bv=js['bufferViews'][a['bufferView']]
    n={'SCALAR':1,'VEC2':2,'VEC3':3,'VEC4':4,'MAT4':16}[a['type']]
    fmt={5126:'f',5123:'H',5121:'B',5122:'h',5120:'b'}[a['componentType']]
    size=struct.calcsize(fmt); stride=bv.get('byteStride',size*n); base=bv.get('byteOffset',0)+a.get('byteOffset',0)
    out=[]
    for k in range(a['count']):
        v=struct.unpack_from('<'+fmt*n,binc,base+k*stride)
        if a.get('normalized'): v=tuple(max(x/{'h':32767,'b':127,'H':65535,'B':255}[fmt],-1.0) for x in v)
        out.append(v if n>1 else v[0])
    return out
class UAL:
    def __init__(self, skel_path, anim_paths):
        self.js,self.b=load(skel_path)
        self.nodes=self.js['nodes']
        self.parent={}
        for i,n in enumerate(self.nodes):
            for c in n.get('children',[]): self.parent[c]=i
        self.name2i={n.get('name'):i for i,n in enumerate(self.nodes)}
        self.joints=[self.nodes[j]['name'] for j in self.js['skins'][0]['joints']]
        self.libs=[load(p) for p in anim_paths]
    def rest_local(self,name):
        n=self.nodes[self.name2i[name]]
        return tuple(n.get('translation',(0,0,0))), tuple(n.get('rotation',(0,0,0,1)))
    def anim(self,name):
        for js,b in self.libs:
            for a in js['animations']:
                if a['name']==name:
                    tr={}
                    for ch in a['channels']:
                        s=a['samplers'][ch['sampler']]
                        nm=js['nodes'][ch['target']['node']]['name']
                        tr.setdefault(nm,{})[ch['target']['path']]=(accessor(js,b,s['input']),accessor(js,b,s['output']))
                    length=max(max(v[0]) for t in tr.values() for v in t.values())
                    return tr,length
        raise KeyError(name)
    def sample(self,tr,t,name,path,default):
        if name not in tr or path not in tr[name]: return default
        times,vals=tr[name][path]
        if t<=times[0]: return tuple(vals[0])
        if t>=times[-1]: return tuple(vals[-1])
        lo,hi=0,len(times)-1
        while hi-lo>1:
            m=(lo+hi)//2
            if times[m]<=t: lo=m
            else: hi=m
        f=(t-times[lo])/(times[hi]-times[lo])
        a,b=vals[lo],vals[hi]
        return slerp(a,b,f) if path=='rotation' else tuple(x+(y-x)*f for x,y in zip(a,b))
    def world(self,tr=None,t=0.0):
        # world (glTF) position & rotation of every node, posed (tr) or rest (None)
        W={}
        def get(i):
            if i in W: return W[i]
            n=self.nodes[i]; nm=n.get('name')
            lt=tuple(n.get('translation',(0,0,0))); lq=tuple(n.get('rotation',(0,0,0,1))); ls=tuple(n.get('scale',(1,1,1)))
            if tr is not None:
                lt=self.sample(tr,t,nm,'translation',lt); lq=self.sample(tr,t,nm,'rotation',lq); ls=self.sample(tr,t,nm,'scale',ls)
            if i in self.parent:
                pp,pq,ps=get(self.parent[i])
                p=add(pp,qrot(pq,(lt[0]*ps[0],lt[1]*ps[1],lt[2]*ps[2]))); q=qmul(pq,lq); s=(ps[0]*ls[0],ps[1]*ls[1],ps[2]*ls[2])
            else: p,q,s=lt,lq,ls
            W[i]=(p,q,s); return W[i]
        out={}
        for nm in self.joints:
            p,q,s=get(self.name2i[nm]); out[nm]=(P(p),qnorm(Pq(q)))
        return out
def qfrom_frame(d, s):
    # rotation whose x axis = d and y axis = s (orthonormalised): returns quat
    d=unit(d); s=unit(sub(s, mul(d, sum(a*b for a,b in zip(s,d)))))
    z=(d[1]*s[2]-d[2]*s[1], d[2]*s[0]-d[0]*s[2], d[0]*s[1]-d[1]*s[0])
    m=[[d[0],s[0],z[0]],[d[1],s[1],z[1]],[d[2],s[2],z[2]]]
    tr=m[0][0]+m[1][1]+m[2][2]
    if tr>0:
        S=math.sqrt(tr+1.0)*2; w=0.25*S; x=(m[2][1]-m[1][2])/S; y=(m[0][2]-m[2][0])/S; zq=(m[1][0]-m[0][1])/S
    elif m[0][0]>m[1][1] and m[0][0]>m[2][2]:
        S=math.sqrt(1.0+m[0][0]-m[1][1]-m[2][2])*2; w=(m[2][1]-m[1][2])/S; x=0.25*S; y=(m[0][1]+m[1][0])/S; zq=(m[0][2]+m[2][0])/S
    elif m[1][1]>m[2][2]:
        S=math.sqrt(1.0+m[1][1]-m[0][0]-m[2][2])*2; w=(m[0][2]-m[2][0])/S; x=(m[0][1]+m[1][0])/S; y=0.25*S; zq=(m[1][2]+m[2][1])/S
    else:
        S=math.sqrt(1.0+m[2][2]-m[0][0]-m[1][1])*2; w=(m[1][0]-m[0][1])/S; x=(m[0][2]+m[2][0])/S; y=(m[1][2]+m[2][1])/S; zq=0.25*S
    return qnorm((x,y,zq,w))
def cross(a,b): return (a[1]*b[2]-a[2]*b[1], a[2]*b[0]-a[0]*b[2], a[0]*b[1]-a[1]*b[0])

class Retargeter:
    def __init__(self, ual, manny_bones):
        # manny_bones: list of dicts name, local{t,q}, world{t,q} in DFS order
        self.u=ual
        self.mb=manny_bones
        self.names=[b['name'] for b in manny_bones]
        self.ref_local={b['name']:(tuple(b['local']['t']),tuple(b['local']['q'])) for b in manny_bones}
        self.ref_world={b['name']:(tuple(b['world']['t']),tuple(b['world']['q'])) for b in manny_bones}
        self.parent=self._infer_parents()
        self.R=ual.world()
        self.k=self.ref_world['pelvis'][0][2]/self.R['pelvis'][0][2]
        self.map={}
        ch=[('spine_01','spine_01',None,0),('spine_02','spine_01','spine_02',0.5),('spine_03','spine_02',None,0),('spine_04','spine_02','spine_03',0.5),('spine_05','spine_03',None,0),
            ('neck_01','neck_01',None,0),('neck_02','neck_01','Head',0.5),('head','Head',None,0),('pelvis','pelvis',None,0)]
        for m,a,b,t in ch: self.map[m]=(a,b,t)
        for side in 'lr':
            for n in ['clavicle','upperarm','lowerarm','hand','thigh','calf','foot','ball']:
                self.map['%s_%s'%(n,side)]=('%s_%s'%(n,side),None,0)
            for f in ['index','middle','ring','pinky','thumb']:
                for k in '123':
                    self.map['%s_0%s_%s'%(f,k,side)]=('%s_0%s_%s'%(f,k,side),None,0)
        self.align=self._alignments()
    def _infer_parents(self):
        par={}
        for i,b in enumerate(self.mb):
            if i==0: par[b['name']]=None; continue
            wt,wq=self.ref_world[b['name']]; lt,lq=self.ref_local[b['name']]
            best=None; be=1e9
            for j in range(i-1,-1,-1):
                pt,pq=self.ref_world[self.names[j]]
                p=add(pt,qrot(pq,lt)); q=qmul(pq,lq)
                e=math.dist(p,wt)+ (1-abs(sum(x*y for x,y in zip(q,wq))))*100
                if e<be-1e-6: be=e; best=self.names[j]
                if e<1e-3: break
            par[b['name']]=best
        return par
    def _alignments(self):
        A={}
        Mw=lambda n: self.ref_world[n][0]
        Uw=lambda n: self.R[n][0]
        for side in 'lr':
            mpalm=sub(Mw('index_01_'+side),Mw('pinky_01_'+side)); upalm=sub(Uw('index_01_'+side),Uw('pinky_01_'+side))
            leftaxis=(1,0,0)
            chains=[(['clavicle','upperarm','lowerarm','hand'],'middle_01',None),(['upperarm','lowerarm','hand'],None,'palm'),
                    ]
            def put(b,child,sec):
                dm=unit(sub(Mw(child),Mw(b))); du=unit(sub(Uw(child),Uw(b)))
                if sec is None:
                    A[b]=arc(dm,du)
                else:
                    sm,su=sec
                    if abs(sum(x*y for x,y in zip(unit(sm),dm)))>0.95 or abs(sum(x*y for x,y in zip(unit(su),du)))>0.95: A[b]=arc(dm,du); return
                    A[b]=qmul(qfrom_frame(du,su),qinv(qfrom_frame(dm,sm)))
            s=lambda n: n+'_'+side
            put(s('clavicle'),s('upperarm'),None)
            put(s('upperarm'),s('lowerarm'),(mpalm,upalm))
            put(s('lowerarm'),s('hand'),(mpalm,upalm))
            put(s('hand'),s('middle_01'),(mpalm,upalm))
            for f in ['index','middle','ring','pinky','thumb']:
                put(s(f+'_01'),s(f+'_02'),(mpalm,upalm))
                put(s(f+'_02'),s(f+'_03'),(mpalm,upalm))
                A[s(f+'_03')]=A[s(f+'_02')]
            put(s('thigh'),s('calf'),(leftaxis,leftaxis))
            put(s('calf'),s('foot'),(leftaxis,leftaxis))
            put(s('foot'),s('ball'),(leftaxis,leftaxis))
            A[s('ball')]=A[s('foot')]
        return A
    def frame(self, tr, t):
        W=self.u.world(tr,t)
        D={n:qmul(W[n][1],qinv(self.R[n][1])) for n in W}
        G={}; L={}
        for n in self.names:
            p=self.parent[n]
            lt,lq=self.ref_local[n]
            if n in self.map:
                a,b,f=self.map[n]
                d=D[a] if b is None else slerp(D[a],D[b],f)
                q=qmul(d,qmul(self.align.get(n,(0,0,0,1)),self.ref_world[n][1]))
                if n=='pelvis':
                    wp=add(self.ref_world['pelvis'][0], mul(sub(W['pelvis'][0],self.R['pelvis'][0]),self.k))
                    pt,pq=G[p]
                    lt=qrot(qinv(pq),sub(wp,pt))
                    G[n]=(wp,q)
                else:
                    pt,pq=G[p]
                    G[n]=(add(pt,qrot(pq,lt)),q)
                lq=qmul(qinv(G[p][1]),q)
            else:
                if p is None: G[n]=(lt,lq)
                else:
                    pt,pq=G[p]; G[n]=(add(pt,qrot(pq,lt)),qmul(pq,lq))
            L[n]=(lt,qnorm(lq))
        return L,G,W

# --- Unreal side ---------------------------------------------------------------------------------
import unreal

# Godot animation name (as the Godot scripts call it) -> UAL clip
ANIMS = {
    'Idle': 'Idle_Loop', 'Walk': 'Walk_Loop', 'Jog_Fwd': 'Jog_Fwd_Loop', 'Sprint': 'Sprint_Loop',
    'Crouch_Idle': 'Crouch_Idle_Loop', 'Crouch_Fwd': 'Crouch_Fwd_Loop',
    'Jump_Start': 'Jump_Start', 'Jump': 'Jump_Loop', 'Jump_Land': 'Jump_Land',
    'Death01': 'Death01', 'Hit_Chest': 'Hit_Chest', 'Hit_Head': 'Hit_Head', 'Hit_Knockback': 'Hit_Knockback',
    'Punch_Jab': 'Punch_Jab', 'Pistol_Aim_Neutral': 'Pistol_Aim_Neutral',
    'Spell_Simple_Enter': 'Spell_Simple_Enter', 'Spell_Simple_Idle': 'Spell_Simple_Idle_Loop', 'Spell_Simple_Shoot': 'Spell_Simple_Shoot',
    'Slide_Start': 'Slide_Start', 'Slide': 'Slide_Loop', 'Slide_Exit': 'Slide_Exit',
    'Melee_Hook': 'Melee_Hook', 'OverhandThrow': 'OverhandThrow',
}
FOLDER = '/Game/WWO/Anims/UAL'
FPS = 30


def log(msg):
    unreal.log_warning('WWO: ' + msg)


def manny_bones():
    skm = unreal.load_asset('/Game/Characters/Mannequins/Meshes/SKM_Manny_Simple')
    ape = unreal.AnimPoseExtensions
    pose = ape.get_reference_pose(skm.skeleton)
    out = []
    for n in ape.get_bone_names(pose):
        lo = ape.get_ref_bone_pose(pose, n, unreal.AnimPoseSpaces.LOCAL)
        wo = ape.get_ref_bone_pose(pose, n, unreal.AnimPoseSpaces.WORLD)
        tr = lambda x: {'t': [x.translation.x, x.translation.y, x.translation.z], 'q': [x.rotation.x, x.rotation.y, x.rotation.z, x.rotation.w]}
        out.append({'name': str(n), 'local': tr(lo), 'world': tr(wo)})
    return skm.skeleton, out


def bake():
    godot = os.path.expanduser(os.environ.get('WWO_GODOT', '~/Documents/WhatWasOurs'))
    ual_dir = os.path.join(godot, 'enemies/models/ual/')
    ual = UAL(ual_dir + 'UAL1_Standard.glb', [ual_dir + 'UAL1_Standard.glb', ual_dir + 'UAL2_Standard.glb'])
    skeleton, bones = manny_bones()
    rt = Retargeter(ual, bones)
    tools = unreal.AssetToolsHelpers.get_asset_tools()
    eal = unreal.EditorAssetLibrary
    for name, clip in ANIMS.items():
        tracks, length = ual.anim(clip)
        frames = max(int(round(length * FPS)), 1)
        keys = {n: ([], []) for n in rt.names}
        for f in range(frames + 1):
            L, _, _ = rt.frame(tracks, min(f / FPS, length))
            for n in rt.names:
                t, q = L[n]
                keys[n][0].append(unreal.Vector(*t))
                keys[n][1].append(unreal.Quat(*q))
        asset = 'A_' + name
        path = FOLDER + '/' + asset
        if eal.does_asset_exist(path):
            seq = eal.load_asset(path)
        else:
            factory = unreal.AnimSequenceFactory()
            factory.set_editor_property('target_skeleton', skeleton)
            seq = tools.create_asset(asset, FOLDER, unreal.AnimSequence, factory)
        ctrl = seq.controller
        ctrl.open_bracket(unreal.Text('Bake ' + name), False)
        ctrl.remove_all_bone_tracks(False)
        ctrl.set_frame_rate(unreal.FrameRate(FPS, 1), False)
        ctrl.set_number_of_frames(unreal.FrameNumber(frames), False)
        ones = [unreal.Vector(1, 1, 1)] * (frames + 1)
        for n in rt.names:
            ctrl.add_bone_curve(n, False)
            ctrl.set_bone_track_keys(n, keys[n][0], keys[n][1], ones, False)
        ctrl.close_bracket(False)
        seq.set_editor_property('enable_root_motion', False)
        eal.save_asset(path, only_if_is_dirty=False)
        log('baked %s (%s, %.3f s, %d frames)' % (asset, clip, length, frames))


bake()
