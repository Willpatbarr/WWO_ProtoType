#pragma once

// Graft anchors: named spots and lines on each shell part (and seams on the bare skeleton) that grafts
// are placed from, so a graft fits whatever parts a robot wears. The data is exported from Blender per
// part (Art/Shells/tools/make_anchors.py, names in anchor_spec.py) to Config/ShellAnchors/<part>.json:
// SK_Shell<n>_<Slot>.json for each part, SK_RobotSkeleton.json for the seams. Every part of a slot has
// the same anchor and path names, so a recipe that names them fits every shell. Each part's file also carries
// its surface ('surface': the skin its anchors and paths lie on, a point every ~2 cm), for placing things between them.
//
// The files are in "blender_rig_cm": the bind pose in centimetres, x = the robot's left, -y = its front,
// z = up, feet at z = 0. Here each point is turned into the robot mesh's own space (FromRig) and then
// into its bone's space in the reference pose, so it follows the bone as the robot animates
// (UWWOBodyComponent::FindAnchor, PathWorld).

#include "CoreMinimal.h"

class USkeletalMesh;

// One anchor, or one point of a path.
struct FWWOShellPoint
{
	FName Bone;
	int32 BoneIndex = INDEX_NONE; // in the mesh the set was made for
	// In the mesh's space, reference pose: where it is, the way out of the surface (X), its 'up' tangent (Z).
	FVector RefP = FVector::ZeroVector, RefN = FVector::ForwardVector, RefUp = FVector::UpVector;
	FTransform Local; // the same against its bone in the reference pose (X out, Z up)
	float Size = 0.f; // cm: about how far the flat spot round it reaches (paths' points: 0)
};

struct FWWOShellPath
{
	bool bClosed = false;
	float Length = 0.f; // cm, as exported
	FName FromSeam, ToSeam; // starts / ends near this seam (SK_RobotSkeleton.json), where another slot's path carries on
	TArray<FWWOShellPoint> Points;
};

// The worn parts' surface (reference pose, mesh space), sampled by each part's 'surface' (its wrap, the skin its
// anchors and paths lie on, thinned to a point every ~2 cm), for placing things away from any anchor: a point is
// pulled onto the surface from its nearest samples (each sample's own tangent plane, weighted by nearness and by
// how alike its normal is), and a walk steps over the surface in short hops, so a piece moved 6 cm round a limb
// stays on it instead of leaving on a tangent.
struct FWWOShellSurface
{
	struct FSample { FVector P, N; FName Bone; };
	TArray<FSample> Samples;

	void Add(const FWWOShellPoint& Point);
	void Add(const FSample& Sample);
	void Finish(); // after the last Add: the lookup grid
	bool IsEmpty() const { return Samples.Num() == 0; }
	// P onto the surface (N: the way out there, in: a hint of it; out: the surface's). False with no samples near.
	bool Project(FVector& P, FVector& N, FName* OutBone = nullptr) const;
	// From P (on the surface, N its normal) Dist cm over the surface toward Dir (made tangent at each hop).
	void Walk(FVector& P, FVector& N, const FVector& Dir, float Dist, FName* OutBone = nullptr) const;

private:
	static constexpr float Cell = 4.f;
	TMap<FIntVector, TArray<int32>> Grid;
	FIntVector Key(const FVector& P) const { return FIntVector(FMath::FloorToInt(P.X / Cell), FMath::FloorToInt(P.Y / Cell), FMath::FloorToInt(P.Z / Cell)); }
};

// One part's anchors and paths (or the skeleton's seams), for one mesh's reference pose.
struct FWWOShellAnchorSet
{
	FName Part;      // SK_Shell2_Core, SK_RobotSkeleton
	FString Slot;    // Head, Core, Arms, Legs, Skeleton
	TMap<FName, FWWOShellPoint> Anchors;
	TMap<FName, FWWOShellPath> Paths;
	TArray<FWWOShellSurface::FSample> Surface; // the part's wrap, a point every ~2 cm (the file's 'surface')
	const USkeletalMesh* RefMesh = nullptr;
};

namespace WWOShellAnchors
{
	// blender_rig_cm to the robot mesh's space (SK_RobotSkeleton's component space): x left, y forward,
	// z up, so only y flips. Checked at run time against the skeleton (Verify, logged once).
	inline FVector FromRig(const FVector& Rig) { return FVector(Rig.X, -Rig.Y, Rig.Z); }

	// Config/ShellAnchors/<Part>.json made for Mesh's reference pose (bones looked up in it), read and
	// converted once a run. Null when there is no such file. Points whose bone the mesh lacks are left out (logged).
	const FWWOShellAnchorSet* Find(FName Part, const USkeletalMesh* Mesh);

	// The reference pose of a bone in the mesh's own space (cached per mesh). Identity for an unknown bone.
	FTransform RefBone(const USkeletalMesh* Mesh, FName Bone);

	// Points along a path (in the mesh's reference pose) every Spacing cm from Start to End (fractions of
	// its length), each in its nearest point's bone: its position, the way out, and the path's direction
	// there as Up. A closed path goes round to its start.
	void Sample(const FWWOShellPath& Path, const USkeletalMesh* Mesh, float Spacing, float Start, float End, TArray<FWWOShellPoint>& Out);

	// A point in the mesh's reference pose (component space: P, X = N, Z = Up) as a point against Bone.
	FWWOShellPoint MakePoint(const USkeletalMesh* Mesh, FName Bone, const FVector& P, const FVector& N, const FVector& Up, float Size);

	// Logs (once a run) how well FromRig puts the seams on the skeleton's joints and the parts within the
	// mesh's height, and what the other axis guesses would have scored.
	void Verify(const USkeletalMesh* Mesh);
}
