#pragma once

// VoltVine's grafts built from the shell parts' paths and anchors (WWOBodyAnchors.cpp, Config/GraftRecipes.json):
// braided bark vines with a glowing core, laid along a line over the parts (a chain of paths, across the seams),
// leaves hanging off them (the fitted grafts' own leaf meshes, SM_VV_Leaf*), curling tendrils, thorns, and glowing crystal pods, in the fitted grafts' own
// materials (MI_VV_*: the bark, the leaf and crystal atlas, the core's glow). The look follows the Blender build of
// the fitted ones (~/Documents/WWO_Flora/tools/make_voltvine_grafts.py: wrap, rope, leaves_along, bulb, tendril).
//
// Everything is built in the robot mesh's reference pose (cm, its own space: x left, y front, z up). Each piece
// rides one bone (Rigid: one mesh per bone, hung on it). A vine whose line passes from one bone to another (an
// elbow, a knee, a shoulder, the spine) would open a gap there if each side were rigid on its bone, so a few cm
// either side of the change are a joint section instead (Joints): one mesh in the model's component space whose
// rings are posed every frame between the two bones' movements, blended along it (FWWOVineJoints::Update).
//
// VeilSpore uses the same meshes: its kit pieces (static meshes, Tools/import_graft_kit.py) copied in where the
// recipe places them (KitPiece), and its mycelium, a net of thin strands grown over the surface along a line
// or round a spot (Net), whose strands cross joints like a vine does. Both in its one atlas material (Fungus).

#include "CoreMinimal.h"
#include "ProceduralMeshComponent.h"

class USkeletalMesh;
class USkeletalMeshComponent;
struct FWWOShellSurface;

// One mesh's geometry, by material.
struct FWWOVineMesh
{
	enum ESection { Bark, Core, Leaf, Crystal, Stem, Fungus, NumSections };
	struct FSection
	{
		TArray<FVector> V, N;
		TArray<FVector2D> UV;
		TArray<FProcMeshTangent> T;
		TArray<int32> Tris;
	};
	FSection S[NumSections];

	bool IsEmpty() const;
	int32 NumTris() const;
	// One mesh section per material used. Vertices as they are (the caller places the component).
	void Build(UProceduralMeshComponent* Mesh) const;
	static UMaterialInterface* Material(ESection Section);
};

// The joint sections of one body's vines: re-posed every frame between their two bones.
struct FWWOVineJoints
{
	struct FPair { int32 A = INDEX_NONE, B = INDEX_NONE; FTransform RefA, RefB; };
	struct FStation { FVector C; float W = 0.f; int32 Pair = 0; };
	TArray<FPair> Pairs;
	TArray<FStation> Stations;
	// Per section: the reference pose (vertices, normals, tangents) and each vertex's station.
	struct FSection
	{
		TArray<FVector> RefV, RefN, V, N;
		TArray<FProcMeshTangent> RefT, T;
		TArray<FVector2D> UV;
		TArray<int32> Station, Tris;
	};
	FSection S[FWWOVineMesh::NumSections];
	TWeakObjectPtr<UProceduralMeshComponent> Comp;
	TWeakObjectPtr<USkeletalMeshComponent> Model;
	FDelegateHandle Handle;
	TArray<int32> SectionOf; // mesh section index -> FWWOVineMesh section
	int32 Updates = 0;
	double Seconds = 0.0; // spent posing them, all told
	// For the log (Stop): per pair, the widest gap a rigid split would have opened at its change of bone so far (cm).
	TArray<float> WidestSplit;

	~FWWOVineJoints() { Stop(); }
	bool IsEmpty() const { return Stations.Num() == 0; }
	int32 NumTris() const;
	// Creates the mesh (in the reference pose) on C and starts following Model's pose (after each animation update).
	void Start(UProceduralMeshComponent* C, USkeletalMeshComponent* InModel);
	void Stop();
	void Update();
	void Report() const; // logs the widest gaps the joint sections closed and their cost (on Stop; in the body test every 1000 updates)
};

// A static mesh's geometry, copied (its LOD 0). A leaf: along +X from its stalk's base, top +Z; Tris[0] the blade,
// [1] the stalk (its VV_Stem slot). A kit piece: everything in Tris[0].
struct FWWOLeafShape
{
	TArray<FVector> V, N, T;
	TArray<FVector2D> UV;
	TArray<int32> Tris[2];
	float Length = 0.f; // its furthest +X
	FBox Bounds = FBox(ForceInit);
};

// VeilSpore's mycelium (FWWOVineGraft::Net): a net of thin strands over the surface, knots every Cell cm or so,
// joined to their neighbours (a beta-skeleton: an irregular net with holes of many sizes), thick in the middle
// and thin at its edge, fine roots off the edge, a knob where three or more meet.
struct FWWONetStyle
{
	FVector2D Width = FVector2D(5.f, 5.f); // cm either side of its line (its across, -/+); round a spot: X its radius
	float Cell = 3.2f;                      // cm between knots, on average
	FVector2D Strand = FVector2D(0.5f, 0.16f); // cm: a strand's radius in its middle, at its edge
	float Clear = 0.05f;                    // cm: a strand's underside off the surface
	FVector2D Taper = FVector2D(0.12f, 0.15f); // fractions of its line over which the width narrows to nothing
	float Holes = 0.18f;                    // the share of knots left out (bigger holes here and there)
	int32 Roots = 6;                        // fine roots off its edge
	float Glow = 1.f;                       // how much it glows near the caps (the atlas's 'net' island: thickness x glow)
};

// A vine's line over the parts: points on the surface (reference pose, mesh space), the way out there, the bone
// under each, and per point what the recipe asked for that stretch (thickness, leaf size, leaves at all).
struct FWWOVineLine
{
	TArray<FVector> P, N;
	TArray<FName> Bone;
	TArray<float> Thick, LeafScale, Leafy;
	int32 Num() const { return P.Num(); }
	void Add(const FVector& InP, const FVector& InN, FName InBone, float InThick, float InLeafScale, float InLeafy);
};

struct FWWOVineStyle
{
	float Radius = 0.78f;     // cm: each strand of the rope (and how far it sits off the line's middle)
	int32 Strands = 3;
	float Pitch = 24.f;       // cm per full twist of the rope
	bool bCore = true;        // the glowing core between the strands
	float Clear = 0.15f;      // cm: the rope's inside edge off the surface
	FVector2D Taper = FVector2D(0.08f, 0.12f); // fractions of its length over which each end thins
	// Leaves: every LeafEvery cm LeafCount whole leaves LeafSize cm long, hanging HangDeg from straight down.
	float LeafEvery = 0.f;
	FIntPoint LeafCount = FIntPoint(1, 2);
	FVector2D LeafSize = FVector2D(9.f, 12.f), HangDeg = FVector2D(5.f, 20.f);
	FVector2D LeafSkip = FVector2D(0.06f, 0.06f); // no leaves this close to the ends (fractions)
	// Crystal pods at these fractions of the length (nudged to face Prefer when it is set), Size as the fitted bulb's scale.
	TArray<float> PodsAt;
	float PodSize = 0.38f;
	FVector PodPrefer = FVector::ZeroVector;
	int32 Tendrils = 0;       // curling shoots spread along it
	bool bTendrilEnd = false; // and one curling off its end
	// A second, thinner rope weaving over this one (WindRadius 0: none), with thorns every ThornEvery cm (0: none).
	float WindRadius = 0.f, WindPitch = 10.f;
	float ThornEvery = 0.f, ThornLength = 2.2f;
};

class FWWOVineGraft
{
public:
	FWWOVineGraft(const USkeletalMesh* InMesh, FRandomStream& InRandom, bool bInJoints);

	// A vine along Line (its points every ~1.2 cm, see Resample).
	void Vine(const FWWOVineLine& Line, const FWWOVineStyle& Style);
	// A crystal pod cradled against the surface at At, pushed out along Out (Scale: the fitted bulb's, 0.4 ~ 12 cm).
	void Crystal(FName Bone, const FVector& At, const FVector& Out, float Scale);
	// Count leaves falling from At outward (Out) and down, fanned along Spread, Size cm.
	void LeafFan(FName Bone, const FVector& At, const FVector& Out, const FVector& Spread, int32 Count, FVector2D Size, float Down);
	// A line wound round a bone (as a vine's line): from its head toward ToBone's at fractions From..To, Turns
	// times round, on an oval Radii cm (X: toward Outward, Y: across), starting at Phase radians from Outward.
	static void BoneWrap(const USkeletalMesh* Mesh, FName Bone, FName ToBone, float From, float To, float Turns, float Phase,
		const FVector2D& Radii, const FVector& Outward, FWWOVineLine& Out);
	// A static mesh's geometry (its LOD 0, which it must keep on the CPU), read once a run. Null if it has none.
	static const FWWOLeafShape* MeshShape(const FString& ObjectPath);
	// A kit piece rigid on Bone: Shape's vertices through Xf (its frame and scale in the mesh's reference pose), in Fungus.
	void KitPiece(FName Bone, const FWWOLeafShape& Shape, const FTransform& Xf);
	// Mycelium along Line (resampled every ~1 cm), Style.Width either side of it, over Surface; Sign -1 mirrors its
	// across. Glow: points it glows near (the caps), with how far that reaches (cm).
	void Net(const FWWOVineLine& Line, const FWWONetStyle& Style, const FWWOShellSurface& Surface, float Sign,
		const TArray<FVector4>& Glow, int32 Seed);
	// Mycelium round one spot (At, its normal N, on Bone), Style.Width.X cm across.
	void NetPatch(FName Bone, const FVector& At, const FVector& N, const FWWONetStyle& Style, const FWWOShellSurface& Surface,
		const TArray<FVector4>& Glow, int32 Seed);
	// The line resampled every Step cm (per-point values carried along).
	static FWWOVineLine Resample(const FWWOVineLine& In, float Step);

	TMap<FName, FWWOVineMesh> Rigid; // by bone
	TSharedPtr<FWWOVineJoints> Joints;
	int32 Pieces = 0;
	int32 KitTris = 0, NetTris = 0; // for the log

private:
	const USkeletalMesh* Mesh;
	FRandomStream& Random;
	bool bJoints;

	// Where each station of a vine goes: rigid on Bone, or in joint section Pair at weight W toward its second bone.
	struct FStationOwner { FName Bone; int32 Pair = INDEX_NONE; float W = 0.f; };
	TArray<FStationOwner> Owners;
	TArray<int32> StationIds; // each station's in Joints (INDEX_NONE: rigid)
	FName BoneAt(int32 Station) const;
	void PlanJoints(const FWWOVineLine& Line, const TArray<FVector>& C);
	// A tube through the stations' points Ring[i] (radius R[i], frame F1/F2 per station), into the rigid meshes and the
	// joint section as each span's owner says. UV V runs VScale per cm along.
	// RingUV: per point one UV for its whole ring (the net's lookup island) instead of round x along.
	void Tube(FWWOVineMesh::ESection Section, const TArray<FVector>& Path, const TArray<float>& R, const TArray<FVector>& F1,
		const TArray<FVector>& F2, int32 Sides, float VScale, const TArray<FVector2D>* RingUV = nullptr);
	// One net's knots and strands (both in the mesh's reference pose), each point owned as Owner says.
	// A knot: on the surface (P, N), its place in the net's own plane (Q: along x across, or round a spot), how
	// near its edge (0 the middle, 1 the edge), the way out of the net there, and where along its line it is.
	struct FNetKnot { FVector P, N, Out; FVector2D Q; float Edge = 0.f, Along = 0.f; FStationOwner Own; };
	void NetBuild(const TArray<FNetKnot>& Knots, const FWWONetStyle& Style, const FWWOShellSurface& Surface,
		const TArray<FVector4>& Glow, FRandomStream& R, const TFunctionRef<FStationOwner(float Along)> OwnerAt);
	int32 FungusTris() const;
	void Leaf(FName Bone, const FVector& At, const FVector& Dir, const FVector& Face, float Length, int32 Kind);
	static const FWWOLeafShape* LeafShape(int32 Kind);
	void Tendril(FName Bone, const FVector& At, const FVector& Out, float Length);
	void Cone(FName Bone, const FVector& At, const FVector& Dir, float Length, float Radius);
	void Strip(FName Bone, FWWOVineMesh::ESection Section, const TArray<FVector>& Path, const TArray<float>& R, int32 Sides);
	FWWOVineMesh& MeshOn(FName Bone) { return Rigid.FindOrAdd(Bone); }
};
