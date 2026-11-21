#pragma once

// VoltVine's grafts built from the shell parts' paths and anchors (WWOBodyAnchors.cpp, Config/GraftRecipes.json):
// braided bark vines with a glowing core, laid along a line over the parts (a chain of paths, across the seams),
// leaves hanging off them (the fitted grafts' own leaf meshes, SM_VV_Leaf*), curling tendrils, thorns, and glowing crystal pods, in the fitted grafts' own
// materials (MI_VV_*: the bark, the leaf and crystal atlas, the core's glow). The look follows the Blender build of
// the fitted ones (~/Documents/WWO_Flora/tools/make_voltvine_grafts.py: wrap, rope, leaves_along, bulb, tendril).
//
// Everything is built in the robot mesh's reference pose (cm, its own space: x left, y front, z up), at every detail
// level at once (FWWOGraftDetail). Each piece rides one bone (Rigid: one mesh per bone and level, made into a static
// mesh with the levels as its LODs and hung on that bone, WWOBodyAnchors.cpp). A vine whose line passes from one bone to another (an
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
class UStaticMesh;
struct FWWOShellSurface;

// Detail levels. Every anchor graft is built at all NumGraftLevels levels at once, from the same placements and
// the same random draws, so a farther level has the same pieces in the same places, only coarser (fewer ring
// sides, fewer rings, a reduced leaf or kit mesh), and the farthest leaves the small pieces out. Each level's
// settings are FWWOGraftDetail (the defaults below, a flora's recipe can override any: GraftRecipes.json "detail").
// A body shows the level its robot's own LOD is at (the shell parts' LODs, made at import), so they step together.
constexpr int32 NumGraftLevels = 3;

struct FWWOGraftDetail
{
	// Vines (VoltVine): each strand's ring sides, the glowing core's, the thorny winding strand's; a ring every
	// Step-th station (stations are 1.2 cm apart; CoreStep for the core). bStrands false: one rope tube the
	// strands' width (RopeSides) instead of the strands, core and winding strand.
	int32 StrandSides = 6, CoreSides = 5, WindSides = 5, Step = 1, CoreStep = 1, RopeSides = 4;
	bool bStrands = true;
	// Leaves: the leaf meshes' LOD (SM_VV_Leaf: 0 full, 1 ~30%, 2 ~14%; 4: a flat card of 2 triangles), the
	// share kept (the same ones at every level: a farther level keeps a subset), the green shoot each grows on
	// (sides, every Nth of its 5 rings; 0 sides: none) and the thin stalk between (of 7 rings).
	int32 LeafLod = 0;
	float LeafKeep = 1.f;
	int32 TwigSides = 6, TwigStep = 1, PetioleSides = 4, PetioleStep = 1;
	// The small pieces: tendrils, thorns, the crystals' cradles and charge arcs, the mycelium's knobs and roots.
	bool bSmall = true;
	int32 SmallSides = 5, SmallStep = 1;
	int32 CrystalRings = 9, CrystalAround = 10;
	// VeilSpore: the kit meshes' LOD (0 full, 1 ~30%, 2 ~10%, 3 ~4%), KitSmallLod's for a piece under KitSmall cm
	// across; a piece smaller than KitMin cm across left out. Its mycelium: ring sides (thick strands, thin ones), a ring every NetStep-th point (1 cm apart), strands
	// thinner than NetMinRadius cm left out, the knobs' sides x rings (0: none).
	int32 KitLod = 0, KitSmallLod = 0;
	float KitMin = 0.f, KitSmall = 0.f;
	int32 NetSides = 5, NetThinSides = 4, NetStep = 1, KnobSides = 6, KnobRings = 4;
	float NetMinRadius = 0.f;
	// IronBark (FWWOGraftBuilder): a plate's rows x columns, a seed pod's sides x rings, its stalk.
	int32 PlateRows = 6, PlateCols = 5, PodSides = 9, PodRings = 9;
	bool bPodStalk = true;

	static FWWOGraftDetail Default(int32 Level);
};

// One mesh's geometry, by material. The last three sections are FWWOGraftBuilder's (IronBark's plates and pods),
// with a second UV set and vertex colors (their materials read both).
struct FWWOVineMesh
{
	enum ESection { Bark, Core, Leaf, Crystal, Stem, Fungus, GraftBark, GraftSeed, GraftVine, NumSections };
	struct FSection
	{
		TArray<FVector> V, N;
		TArray<FVector2D> UV, UV1;
		TArray<FColor> C;
		TArray<FProcMeshTangent> T;
		TArray<int32> Tris;
	};
	FSection S[NumSections];

	bool IsEmpty() const;
	int32 NumTris() const;
	static UMaterialInterface* Material(ESection Section);
	static FString MaterialPath(ESection Section); // its object path
};

// One bone's mesh at every detail level.
struct FWWOVineMeshLevels
{
	FWWOVineMesh L[NumGraftLevels];
	bool IsEmpty() const { return L[0].IsEmpty(); }
};

// The joint sections of one body's vines: re-posed every frame between their two bones. The stations are the same
// at every level; each level has its own rings round them, and only the level shown is posed and drawn.
struct FWWOVineJoints
{
	struct FPair { int32 A = INDEX_NONE, B = INDEX_NONE; FTransform RefA, RefB; };
	struct FStation { FVector C; float W = 0.f; int32 Pair = 0; };
	TArray<FPair> Pairs;
	TArray<FStation> Stations;
	// Per level and section: the reference pose (vertices, normals, tangents) and each vertex's station.
	struct FSection
	{
		TArray<FVector> RefV, RefN, V, N;
		TArray<FProcMeshTangent> RefT, T;
		TArray<FVector2D> UV;
		TArray<int32> Station, Tris;
	};
	FSection S[NumGraftLevels][FWWOVineMesh::NumSections];
	int32 Level = 0; // the level shown
	TWeakObjectPtr<UProceduralMeshComponent> Comp;
	TWeakObjectPtr<USkeletalMeshComponent> Model;
	FDelegateHandle Handle;
	TArray<int32> SectionOf; // mesh section index -> FWWOVineMesh section
	int32 Updates = 0;
	double Seconds = 0.0; // spent posing them, all told
	// For the log (Stop): per pair, the widest gap a rigid split would have opened at its change of bone so far (cm).
	TArray<float> WidestSplit;

	FWWOVineJoints() = default;
	FWWOVineJoints(const FWWOVineJoints& Other); // the geometry only (a body's own copy of a built one), not started
	~FWWOVineJoints() { Stop(); }
	bool IsEmpty() const { return Stations.Num() == 0; }
	int32 NumTris(int32 InLevel = 0) const;
	// Creates the mesh (in the reference pose, at InLevel) on C and starts following Model's pose (after each animation update).
	void Start(UProceduralMeshComponent* C, USkeletalMeshComponent* InModel, int32 InLevel);
	// Shows another level (its sections replace the last level's).
	void SetLevel(int32 InLevel);
	void Stop();
	void Update();
	void Report() const; // logs the widest gaps the joint sections closed and their cost (on Stop; in the body test every 1000 updates)
private:
	void CreateSections();
};

// A static mesh's geometry, copied (one of its LODs). A leaf: along +X from its stalk's base, top +Z; Tris[0] the
// blade, [1] the stalk (its VV_Stem slot). A kit piece: everything in Tris[0].
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
	FWWOVineGraft(const USkeletalMesh* InMesh, FRandomStream& InRandom, bool bInJoints, const FWWOGraftDetail (&InDetail)[NumGraftLevels]);

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
	// A kit mesh's geometry (LOD Lod, or its last if it has fewer), as ReadMesh read it. Null if it has none.
	static const FWWOLeafShape* MeshShape(const FString& ObjectPath, int32 Lod = 0);
	// Copies a static mesh's LODs 0-3 (it must keep them on the CPU), once a run, and the leaves' (SM_VV_Leaf at
	// LeafPaths): from any thread, the meshes loaded (and kept from garbage collection) by the game thread.
	static void ReadMesh(const FString& ObjectPath, const UStaticMesh* Mesh);
	static void ReadLeaves(TConstArrayView<const UStaticMesh*> Meshes);
	static TArray<FString> LeafPaths();
	// A kit piece rigid on Bone: Shapes' vertices (one per level: its KitLod; SmallShapes: its KitSmallLod) through Xf
	// (its frame and scale in the mesh's reference pose), in Fungus. Left out of a level where it is smaller than KitMin.
	void KitPiece(FName Bone, const FWWOLeafShape* const (&Shapes)[NumGraftLevels], const FWWOLeafShape* const (&SmallShapes)[NumGraftLevels],
		const FTransform& Xf);
	// Mycelium along Line (resampled every ~1 cm), Style.Width either side of it, over Surface; Sign -1 mirrors its
	// across. Glow: points it glows near (the caps), with how far that reaches (cm).
	void Net(const FWWOVineLine& Line, const FWWONetStyle& Style, const FWWOShellSurface& Surface, float Sign,
		const TArray<FVector4>& Glow, int32 Seed);
	// Mycelium round one spot (At, its normal N, on Bone), Style.Width.X cm across.
	void NetPatch(FName Bone, const FVector& At, const FVector& N, const FWWONetStyle& Style, const FWWOShellSurface& Surface,
		const TArray<FVector4>& Glow, int32 Seed);
	// The line resampled every Step cm (per-point values carried along).
	static FWWOVineLine Resample(const FWWOVineLine& In, float Step);

	// Where the net may not go (the worn parts' obstacles, FWWOShellVolume): its knots there are left out.
	TFunction<bool(const FVector&)> Blocked;
	int32 KnotsBlocked = 0; // for the log

	TMap<FName, FWWOVineMeshLevels> Rigid; // by bone
	TSharedPtr<FWWOVineJoints> Joints;
	int32 Pieces = 0;
	int32 KitTris[NumGraftLevels] = { 0, 0, 0 }, NetTris[NumGraftLevels] = { 0, 0, 0 }; // for the log
	int32 Leaves = 0, LeavesKept[NumGraftLevels] = { 0, 0, 0 };
	FWWOGraftDetail Detail[NumGraftLevels];

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
	// Per level: ring sides (fewer than 3: none at that level) and every how many stations a ring.
	struct FRings { int32 Sides[NumGraftLevels]; int32 Step[NumGraftLevels]; };
	// A tube through the stations' points Ring[i] (radius R[i], frame F1/F2 per station), into the rigid meshes and the
	// joint section as each span's owner says, at every level as Rings says. UV V runs VScale per cm along.
	// RingUV: per point one UV for its whole ring (the net's lookup island) instead of round x along.
	void Tube(FWWOVineMesh::ESection Section, const TArray<FVector>& Path, const TArray<float>& R, const TArray<FVector>& F1,
		const TArray<FVector>& F2, const FRings& Rings, float VScale, const TArray<FVector2D>* RingUV = nullptr);
	FRings SmallRings(int32 Sides0) const; // a small piece: Sides0 sides, then the levels' small pieces' (none where bSmall is off)
	// One net's knots and strands (both in the mesh's reference pose), each point owned as Owner says.
	// A knot: on the surface (P, N), its place in the net's own plane (Q: along x across, or round a spot), how
	// near its edge (0 the middle, 1 the edge), the way out of the net there, and where along its line it is.
	struct FNetKnot { FVector P, N, Out; FVector2D Q; float Edge = 0.f, Along = 0.f; FStationOwner Own; };
	void NetBuild(const TArray<FNetKnot>& Knots, const FWWONetStyle& Style, const FWWOShellSurface& Surface,
		const TArray<FVector4>& Glow, FRandomStream& R, const TFunctionRef<FStationOwner(float Along)> OwnerAt);
	int32 FungusTris(int32 Level) const;
	void Leaf(FName Bone, const FVector& At, const FVector& Dir, const FVector& Face, float Length, int32 Kind);
	static const FWWOLeafShape* LeafShape(int32 Kind, int32 Lod = 0);
	uint32 LeafSerial = 0;
	void Tendril(FName Bone, const FVector& At, const FVector& Out, float Length);
	void Cone(FName Bone, const FVector& At, const FVector& Dir, float Length, float Radius);
	void Strip(FName Bone, FWWOVineMesh::ESection Section, const TArray<FVector>& Path, const TArray<float>& R, const FRings& Rings);
	FWWOVineMesh& MeshOn(FName Bone, int32 Level) { return Rigid.FindOrAdd(Bone).L[Level]; }
};
