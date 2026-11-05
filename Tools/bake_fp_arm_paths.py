"""Bakes the first-person arm motion from the Godot build's UAL animations into
Source/WhatWasOurs/Player/WWOFPArmPaths.h (plain Python, no dependencies).

  python3 Tools/bake_fp_arm_paths.py ~/Documents/WhatWasOurs

Plays Melee_Hook and OverhandThrow (UAL2_Standard.glb) on the UAL1_Standard.glb skeleton, like
characters/fp_arm.gd + extra_anims.gd, and writes the right arm's shoulder, elbow, wrist and
knuckle positions relative to the eyes at 60 Hz.
"""
import json, math, os, struct, sys
def load(path):
    b = open(path,'rb').read()
    magic, ver, length = struct.unpack_from('<4sII', b, 0)
    off = 12; js = None; binc = None
    while off < length:
        clen, ctype = struct.unpack_from('<I4s', b, off); off += 8
        chunk = b[off:off+clen]; off += clen
        if ctype == b'JSON': js = json.loads(chunk)
        elif ctype == b'BIN\x00': binc = chunk
    return js, binc
def accessor(js, binc, i):
    a = js['accessors'][i]; bv = js['bufferViews'][a['bufferView']]
    n = {'SCALAR':1,'VEC2':2,'VEC3':3,'VEC4':4,'MAT4':16}[a['type']]
    fmt = {5126:'f',5123:'H',5121:'B',5122:'h',5120:'b'}[a['componentType']]
    size = struct.calcsize(fmt)
    stride = bv.get('byteStride', size*n)
    base = bv.get('byteOffset',0) + a.get('byteOffset',0)
    out = []
    for k in range(a['count']):
        v = struct.unpack_from('<'+fmt*n, binc, base + k*stride)
        if a.get('normalized'):
            v = tuple(max(x/ {'h':32767,'b':127,'H':65535,'B':255}[fmt], -1.0) for x in v)
        out.append(v if n>1 else v[0])
    return out
U = os.path.join(os.path.expanduser(sys.argv[1] if len(sys.argv) > 1 else '~/Documents/WhatWasOurs'), 'enemies/models/ual/')
js1,b1 = load(U+'UAL1_Standard.glb')
js2,b2 = load(U+'UAL2_Standard.glb')
nodes = js1['nodes']
parent = {}
for i,n in enumerate(nodes):
    for c in n.get('children',[]): parent[c]=i
name2i = {n.get('name'):i for i,n in enumerate(nodes)}
joints = set(js1['skins'][0]['joints'])
def qmul(a,b):
    ax,ay,az,aw=a; bx,by,bz,bw=b
    return (aw*bx+ax*bw+ay*bz-az*by, aw*by-ax*bz+ay*bw+az*bx, aw*bz+ax*by-ay*bx+az*bw, aw*bw-ax*bx-ay*by-az*bz)
def qrot(q,v):
    x,y,z,w=q; vx,vy,vz=v
    # v' = q v q*
    tx=2*(y*vz-z*vy); ty=2*(z*vx-x*vz); tz=2*(x*vy-y*vx)
    return (vx+w*tx+(y*tz-z*ty), vy+w*ty+(z*tx-x*tz), vz+w*tz+(x*ty-y*tx))
def slerp(a,b,t):
    d=sum(x*y for x,y in zip(a,b))
    if d<0: b=tuple(-x for x in b); d=-d
    if d>0.9995:
        r=tuple(x+(y-x)*t for x,y in zip(a,b)); n=math.sqrt(sum(x*x for x in r)); return tuple(x/n for x in r)
    th=math.acos(d); s=math.sin(th)
    return tuple((math.sin((1-t)*th)*x+math.sin(t*th)*y)/s for x,y in zip(a,b))
def rest(i):
    n=nodes[i]; return [tuple(n.get('translation',(0,0,0))), tuple(n.get('rotation',(0,0,0,1))), tuple(n.get('scale',(1,1,1)))]
def tracks(anim_name):
    a=[x for x in js2['animations'] if x['name']==anim_name][0]
    out={}
    for ch in a['channels']:
        s=a['samplers'][ch['sampler']]
        nm=js2['nodes'][ch['target']['node']]['name']
        out.setdefault(nm,{})[ch['target']['path']]=(accessor(js2,b2,s['input']),accessor(js2,b2,s['output']),s.get('interpolation','LINEAR'))
    return out
def sample(tr,t):
    times,vals,interp=tr
    if t<=times[0]: return vals[0]
    if t>=times[-1]: return vals[-1]
    lo,hi=0,len(times)-1
    while hi-lo>1:
        m=(lo+hi)//2
        if times[m]<=t: lo=m
        else: hi=m
    if interp=='STEP': return vals[lo]
    f=(t-times[lo])/(times[hi]-times[lo])
    a,b=vals[lo],vals[hi]
    if len(a)==4: return slerp(a,b,f)
    return tuple(x+(y-x)*f for x,y in zip(a,b))
def pose(trk,t):
    loc={}
    for i in range(len(nodes)):
        r=rest(i); nm=nodes[i].get('name')
        if nm in trk:
            for path,idx in (('translation',0),('rotation',1),('scale',2)):
                if path in trk[nm]: r[idx]=tuple(sample(trk[nm][path],t))
        loc[i]=r
    glob={}
    def g(i):
        if i in glob: return glob[i]
        T,R,S=loc[i]
        p=parent.get(i)
        if p is None or p not in joints:
            glob[i]=(T,R,S); return glob[i]
        PT,PR,PS=g(p)
        st=(T[0]*PS[0],T[1]*PS[1],T[2]*PS[2])
        pos=qrot(PR,st); pos=(pos[0]+PT[0],pos[1]+PT[1],pos[2]+PT[2])
        glob[i]=(pos,qmul(PR,R),tuple(a*b for a,b in zip(PS,S)))
        return glob[i]
    return {nodes[i].get('name'):g(i)[0] for i in joints}

EYE_OFFSET = (0.0, 0.07, 0.09) # fp_arm.gd
BONES = ['upperarm_r', 'lowerarm_r', 'hand_r', 'middle_01_r']


def bake(anim, end):
    trk = tracks(anim)
    rows = []
    for k in range(int(round(end * 60)) + 1):
        p = pose(trk, min(k / 60, end))
        eye = tuple(a + b for a, b in zip(p['Head'], EYE_OFFSET))
        rows.append([tuple(a - b for a, b in zip(p[bone], eye)) for bone in BONES])
    return rows


def table(name, rows):
    s = "\tinline const FVector %s[][4] = {\n" % name
    for r in rows:
        s += "\t\t{ " + ", ".join("FVector(%.4ff, %.4ff, %.4ff)" % tuple(v) for v in r) + " },\n"
    return s + "\t};\n"


hook_length = max(v[0][-1] for d in tracks('Melee_Hook').values() for v in d.values())
header = '''#pragma once

// The first-person arms' motion, baked from the Godot build's animations (Quaternius Universal
// Animation Library 2, CC0: Melee_Hook and OverhandThrow, played on the UAL1 mannequin skeleton,
// as characters/fp_arm.gd does). Generated by Tools/bake_fp_arm_paths.py; don't edit by hand.
// Each row is one 60 Hz frame: the right arm's upperarm_r (shoulder), lowerarm_r (elbow),
// hand_r (wrist) and middle_01_r (knuckles) in Godot model space (meters, y up, the model facing
// +z), relative to the eyes (the Head bone's position that frame + fp_arm.gd's EYE_OFFSET).

#include "CoreMinimal.h"

namespace WWOArmPaths
{
	constexpr float FrameRate = 60.f;
	constexpr float MeleeHookLength = %.6ff; // seconds
%s
	// OverhandThrow, first 0.42 s (the throw only uses that much).
%s}
''' % (hook_length, table('MeleeHook', bake('Melee_Hook', hook_length)), table('OverhandThrow', bake('OverhandThrow', 0.42)))
out = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'Source', 'WhatWasOurs', 'Player', 'WWOFPArmPaths.h')
open(out, 'w').write(header)
print('wrote', out)
