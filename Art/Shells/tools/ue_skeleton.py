# Reads the bones of an Unreal skeleton asset (USkeleton .uasset, UE 4.19 to 4.26 packages) with no Unreal
# installed: names, parents and rest transforms. Plain Python, so it runs inside or outside Blender.
#   python3 ue_skeleton.py <Skeleton.uasset>      prints the tree with each bone's position (Unreal space, cm)
# Unreal space is left-handed: X right of a character facing +Y, Z up. to_blender() gives the same pose in
# Blender's space (the character faces -Y), which is what an FBX round trip through Unreal produces.
import struct, sys


class Bone:
    __slots__ = ("name", "parent", "quat", "pos", "scale", "wquat", "wpos")


def _qmul(a, b):
    ax, ay, az, aw = a; bx, by, bz, bw = b
    return (aw * bx + ax * bw + ay * bz - az * by, aw * by - ax * bz + ay * bw + az * bx,
            aw * bz + ax * by - ay * bx + az * bw, aw * bw - ax * bx - ay * by - az * bz)


def _qrot(q, v):
    x, y, z, w = q; vx, vy, vz = v
    tx = 2 * (y * vz - z * vy); ty = 2 * (z * vx - x * vz); tz = 2 * (x * vy - y * vx)
    return (vx + w * tx + y * tz - z * ty, vy + w * ty + z * tx - x * tz, vz + w * tz + x * ty - y * tx)


def read(path):
    """The skeleton's bones in asset order (a parent always comes before its children).
    quat (x, y, z, w) and pos are relative to the parent; wquat and wpos are in the skeleton's own space."""
    d = open(path, "rb").read()
    # FReferenceSkeleton: count, then per bone { FName, parent index, export name }, then count and one
    # FTransform (quat, translation, scale: ten floats) per bone. The root is the only bone with parent -1.
    i = d.lower().find(b"\xff\xff\xff\xff\x05\x00\x00\x00root\x00")
    if i < 0: raise ValueError("no bone table found in " + path)
    p = i - 12
    n = struct.unpack_from("<i", d, p)[0]; p += 4
    if not 0 < n < 2000: raise ValueError("unlikely bone count %d in %s" % (n, path))
    bones = []
    for k in range(n):
        _, _, par, ln = struct.unpack_from("<iiii", d, p); p += 16
        b = Bone(); b.name = d[p:p + ln - 1].decode("latin1"); b.parent = par; p += ln
        if not -1 <= par < k: raise ValueError("bone %d (%s) has parent %d" % (k, b.name, par))
        bones.append(b)
    if struct.unpack_from("<i", d, p)[0] != n: raise ValueError("pose count differs from bone count in " + path)
    p += 4
    for b in bones:
        t = struct.unpack_from("<10f", d, p); p += 40
        b.quat, b.pos, b.scale = t[0:4], t[4:7], t[7:10]
        if b.parent < 0: b.wquat, b.wpos = b.quat, b.pos
        else:
            q = bones[b.parent]
            r = _qrot(q.wquat, b.pos)
            b.wquat = _qmul(q.wquat, b.quat); b.wpos = (q.wpos[0] + r[0], q.wpos[1] + r[1], q.wpos[2] + r[2])
    return bones


def to_blender(quat, pos):
    """An Unreal rotation and position mirrored across Y into Blender's right-handed space."""
    x, y, z, w = quat
    return (-x, y, -z, w), (pos[0], -pos[1], pos[2])


if __name__ == "__main__":
    for path in sys.argv[1:]:
        bones = read(path); depth = []
        print(path.split("/")[-1], len(bones), "bones")
        for b in bones:
            depth.append(0 if b.parent < 0 else depth[b.parent] + 1)
            print("%s%-30s %7.1f %7.1f %7.1f" % ("  " * depth[-1], b.name, *b.wpos))
