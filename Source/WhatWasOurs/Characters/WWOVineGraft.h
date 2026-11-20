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

#include "CoreMinimal.h"
#include "ProceduralMeshComponent.h"

class USkeletalMesh;
class USkeletalMeshComponent;

// One mesh's geometry, by material.
struct FWWOVineMesh
{
	enum ESection { Bark, Core, Leaf, Crystal, Stem, NumSections };
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

// One leaf's geometry (a static mesh's, copied): along +X from its stalk's base, top +Z; Tris[0] the blade, [1] the stalk.
struct FWWOLeafShape
{
	TArray<FVector> V, N, T;
	TArray<FVector2D> UV;
	TArray<int32> Tris[2];
	float Length = 0.f;
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
	// The line resampled every Step cm (per-point values carried along).
	static FWWOVineLine Resample(const FWWOVineLine& In, float Step);

	TMap<FName, FWWOVineMesh> Rigid; // by bone
	TSharedPtr<FWWOVineJoints> Joints;
	int32 Pieces = 0;

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
	void Tube(FWWOVineMesh::ESection Section, const TArray<FVector>& Path, const TArray<float>& R, const TArray<FVector>& F1,
		const TArray<FVector>& F2, int32 Sides, float VScale);
	void Leaf(FName Bone, const FVector& At, const FVector& Dir, const FVector& Face, float Length, int32 Kind);
	static const FWWOLeafShape* LeafShape(int32 Kind);
	void Tendril(FName Bone, const FVector& At, const FVector& Out, float Length);
	void Cone(FName Bone, const FVector& At, const FVector& Dir, float Length, float Radius);
	void Strip(FName Bone, FWWOVineMesh::ESection Section, const TArray<FVector>& Path, const TArray<float>& R, int32 Sides);
	FWWOVineMesh& MeshOn(FName Bone) { return Rigid.FindOrAdd(Bone); }
};
