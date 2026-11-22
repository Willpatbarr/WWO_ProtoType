// The body's graft anchors (the worn shell parts' anchors and paths, Characters/WWOShellAnchors.h) and the
// grafts placed from them: a flora's pieces from Config/GraftRecipes.json, built on whatever parts are worn
// and placed again whenever a part is swapped: procedural bark plates and seed pods (FWWOGraftBuilder),
// VoltVine's vines along chains of paths with their leaves, tendrils and crystal pods (FWWOVineGraft), and
// VeilSpore's kit pieces (static meshes: shelf fungi, its hood, caps) laid on anchors and along paths, with its
// mycelium grown over the surface between them (FWWOVineGraft::KitPiece, Net). Plates and pods can be seated on the
// worn parts' surface (FWWOShellSurface::Seat), and every piece keeps off the parts' obstacles (an antenna).
//
// Cost. Everything is built at three detail levels at once (FWWOGraftDetail, WWOVineGraft.h), on a worker thread
// (BuildGeometry: nothing there touches the body or loads anything; what it reads was read on the game thread
// first). The result is one static mesh per bone with the three levels as its LODs, made on the game thread a few
// at a time (no frame spends more than wwo.AnchorGraftBudgetMs on them), and kept per flora, kit and worn parts
// (FWWOAnchorGraftCache): a second robot wearing the same is given the same meshes, so it costs only its components.
// The old grafts stay until the new ones are ready (a part swap never leaves the robot bare). Each body shows the
// level its robot's own LOD is at (the shell parts follow the same LOD: SK_RobotSkeleton's three, made at import),
// so the flora steps down with the parts. Vines crossing a joint are one small procedural mesh per body, posed
// every frame (FWWOVineJoints), at the level shown.

#include "Characters/WWOBodyComponent.h"
#include "Characters/WWOShellAnchors.h"
#include "Characters/WWOGraftBuilder.h"
#include "Characters/WWOVineGraft.h"
#include "Systems/WWOConfig.h"
#include "WWOSwitches.h"
#include "WhatWasOurs.h"
#include "WWOTypes.h"
#include "Engine/World.h"
#include "Engine/StaticMesh.h"
#include "PhysicsEngine/BodySetup.h"
#include "Components/StaticMeshComponent.h"
#include "ProceduralMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/PlayerController.h"
#include "Camera/PlayerCameraManager.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/SkinnedAssetCommon.h"
#include "HAL/IConsoleManager.h"
#include "UObject/UObjectIterator.h"
#include "UObject/GCObject.h"
#include "UObject/Package.h"
#include "Containers/Ticker.h"
#include "Tasks/Task.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UObjectGlobals.h"
#include "Misc/PackageName.h"
#include "MeshDescription.h"
#include "StaticMeshAttributes.h"

namespace
{
	const FName SeamPart(TEXT("SK_RobotSkeleton"));

	// wwo.AnchorGrafts: -1 each model's "anchor_grafts" (Config/CharacterModels.json), 0 never, 1 on every robot.
	int32 AnchorGraftsMode = -1;
	FAutoConsoleVariableRef AnchorGraftsVar(TEXT("wwo.AnchorGrafts"), AnchorGraftsMode,
		TEXT("Flora grafts placed from the shell parts' anchors (Config/GraftRecipes.json) instead of the fitted ones: 1 on every robot, 0 never, -1 per model (CharacterModels.json anchor_grafts). -WWOAnchorGrafts: 1 from the start."),
		FConsoleVariableDelegate::CreateLambda([](IConsoleVariable*) { UWWOBodyComponent::RebuildAllGrafts(nullptr); }));

	// wwo.AnchorVineJoints: 1 a vine passing from one bone to the next is posed between them there every frame;
	// 0 each stretch rigid on its own bone (to compare: the gaps that opens at elbows, knees and the spine).
	int32 VineJointsMode = 1;
	FAutoConsoleVariableRef VineJointsVar(TEXT("wwo.AnchorVineJoints"), VineJointsMode,
		TEXT("Anchor grafts' vines (VoltVine): 1 posed between the two bones where they pass a joint, every frame; 0 rigid on each bone (gaps open at bent joints)."),
		FConsoleVariableDelegate::CreateLambda([](IConsoleVariable*) { UWWOBodyComponent::RebuildAllGrafts(nullptr); }));

	// wwo.AnchorGraftLevel: -1 each robot's grafts at the level its own LOD is at (with the shell parts); 0-2 that level
	// on every robot (to look at one).
	int32 ForcedLevel = -1;
	FAutoConsoleVariableRef ForcedLevelVar(TEXT("wwo.AnchorGraftLevel"), ForcedLevel,
		TEXT("Anchor grafts' detail level: -1 (default) each robot's at its own LOD, as its shell parts are; 0 (closest) to 2 (farthest) on every robot. -WWOAnchorGraftLevel=<n> from the start."));

	// wwo.AnchorGraftBudgetMs: how long one frame may spend making the built grafts' meshes and giving them to robots
	// (it always does at least one).
	float BudgetMs = 2.5f;
	FAutoConsoleVariableRef BudgetVar(TEXT("wwo.AnchorGraftBudgetMs"), BudgetMs,
		TEXT("Anchor grafts: the most one frame spends making their meshes and handing them to robots, ms (at least one each frame)."));

	int32 AnchorGraftsNow()
	{
		static bool bRead = false;
		if (!bRead)
		{
			bRead = true;
			if (AnchorGraftsMode < 0 && WWOSwitches::Has(TEXT("WWOAnchorGrafts"))) AnchorGraftsMode = 1;
		int32 Level = -1;
		if (WWOSwitches::Value(TEXT("WWOAnchorGraftLevel="), Level)) ForcedLevel = FMath::Clamp(Level, -1, NumGraftLevels - 1);
		}
		return AnchorGraftsMode;
	}

	// One flora's recipe: per grenade ("grenade", any grenade) and per ability id, a list of placements.
	TSharedPtr<FJsonObject> Recipe(FName Flora)
	{
		const TSharedPtr<FJsonObject> Json = WWOConfig::LoadCached(TEXT("GraftRecipes.json"));
		const TSharedPtr<FJsonObject>* Floras = nullptr;
		const TSharedPtr<FJsonObject>* Found = nullptr;
		if (Json.IsValid() && !Flora.IsNone() && Json->TryGetObjectField(TEXT("floras"), Floras) && (*Floras)->TryGetObjectField(Flora.ToString(), Found)) return *Found;
		return nullptr;
	}

	double Num(const TSharedPtr<FJsonObject>& O, const TCHAR* Field, double Default)
	{
		double V = Default;
		O->TryGetNumberField(Field, V);
		return V;
	}

	bool Flag(const TSharedPtr<FJsonObject>& O, const TCHAR* Field, bool Default)
	{
		bool V = Default;
		O->TryGetBoolField(Field, V);
		return V;
	}

	FVector2D Pair(const TSharedPtr<FJsonObject>& O, const TCHAR* Field, const FVector2D& Default)
	{
		const FVector V = WWOConfig::Vec(O, Field, FVector(Default.X, Default.Y, 0.0));
		return FVector2D(V.X, V.Y);
	}

	TSharedPtr<FJsonObject> Object(const TSharedPtr<FJsonObject>& O, const TCHAR* Field)
	{
		const TSharedPtr<FJsonObject>* Found = nullptr;
		return O->TryGetObjectField(Field, Found) ? *Found : nullptr;
	}

	// Does a placement name anything with '{s}' (once per side)?
	bool IsSided(const TSharedPtr<FJsonObject>& O)
	{
		for (const TCHAR* Field : { TEXT("anchor"), TEXT("path"), TEXT("bone") })
		{
			FString Name;
			if (O->TryGetStringField(Field, Name) && Name.Contains(TEXT("{s}"))) return true;
		}
		const TArray<TSharedPtr<FJsonValue>>* Paths = nullptr;
		if (O->TryGetArrayField(TEXT("paths"), Paths))
		{
			for (const TSharedPtr<FJsonValue>& V : *Paths)
			{
				FString Name;
				if (V->AsObject().IsValid() && V->AsObject()->TryGetStringField(TEXT("path"), Name) && Name.Contains(TEXT("{s}"))) return true;
			}
		}
		return false;
	}

	// A direction word in the mesh's space (x left, y front, z up); 'outer' is away from the middle on Sign's side.
	FVector Word(const FString& W, float Sign)
	{
		if (W == TEXT("front")) return FVector(0, 1, 0);
		if (W == TEXT("back")) return FVector(0, -1, 0);
		if (W == TEXT("outer")) return FVector(Sign, 0, 0);
		if (W == TEXT("up")) return FVector(0, 0, 1);
		if (W == TEXT("down")) return FVector(0, 0, -1);
		return FVector::ZeroVector;
	}

	// A vine's look from its placement (FWWOVineStyle; the defaults are the fitted limb vines').
	FWWOVineStyle VineStyle(const TSharedPtr<FJsonObject>& O, float Sign)
	{
		FWWOVineStyle St;
		St.Radius = Num(O, TEXT("radius"), St.Radius);
		St.Strands = FMath::Clamp(int32(Num(O, TEXT("strands"), St.Strands)), 1, 4);
		St.Pitch = FMath::Max(Num(O, TEXT("pitch"), St.Pitch), 4.0);
		St.bCore = Flag(O, TEXT("core"), St.bCore);
		St.Clear = Num(O, TEXT("clear"), St.Clear);
		St.Taper = Pair(O, TEXT("taper"), St.Taper);
		if (const TSharedPtr<FJsonObject> L = Object(O, TEXT("leaves")))
		{
			St.LeafEvery = FMath::Max(Num(L, TEXT("every"), 4.0), 1.0);
			const FVector2D Count = Pair(L, TEXT("count"), FVector2D(St.LeafCount));
			St.LeafCount = FIntPoint(FMath::RoundToInt(Count.X), FMath::Max(FMath::RoundToInt(Count.Y), FMath::RoundToInt(Count.X)));
			St.LeafSize = Pair(L, TEXT("size"), St.LeafSize);
			St.HangDeg = Pair(L, TEXT("hang"), St.HangDeg);
			St.LeafSkip = Pair(L, TEXT("skip"), St.LeafSkip);
		}
		if (const TSharedPtr<FJsonObject> P = Object(O, TEXT("pods")))
		{
			const TArray<TSharedPtr<FJsonValue>>* At = nullptr;
			if (P->TryGetArrayField(TEXT("at"), At)) for (const TSharedPtr<FJsonValue>& V : *At) St.PodsAt.Add(float(V->AsNumber()));
			St.PodSize = Num(P, TEXT("size"), St.PodSize);
			FString Prefer;
			if (P->TryGetStringField(TEXT("prefer"), Prefer)) St.PodPrefer = Word(Prefer, Sign);
		}
		St.Tendrils = int32(Num(O, TEXT("tendrils"), 0.0));
		St.bTendrilEnd = Flag(O, TEXT("tendril_end"), false);
		if (const TSharedPtr<FJsonObject> W = Object(O, TEXT("wind")))
		{
			St.WindRadius = Num(W, TEXT("radius"), 0.5);
			St.WindPitch = FMath::Max(Num(W, TEXT("pitch"), 10.0), 3.0);
			St.ThornEvery = Num(W, TEXT("thorns"), 0.0);
			St.ThornLength = Num(W, TEXT("thorn_length"), St.ThornLength);
		}
		return St;
	}

	bool IsVinePiece(const FString& Piece)
	{
		return Piece == TEXT("vine") || Piece == TEXT("bone_wrap") || Piece == TEXT("crystal") || Piece == TEXT("leaf_fan");
	}

	// A number or [a, b] (a number: both).
	FVector2D NumOrPair(const TSharedPtr<FJsonObject>& O, const TCHAR* Field, const FVector2D& Default)
	{
		double V = 0.0;
		if (O->TryGetNumberField(Field, V)) return FVector2D(V, V);
		return Pair(O, Field, Default);
	}

	// VeilSpore's mycelium from its placement (FWWONetStyle).
	FWWONetStyle NetStyle(const TSharedPtr<FJsonObject>& O)
	{
		FWWONetStyle St;
		St.Width = NumOrPair(O, TEXT("width"), St.Width);
		if (O->HasField(TEXT("size"))) St.Width = FVector2D(Num(O, TEXT("size"), 8.0));
		St.Cell = FMath::Max(Num(O, TEXT("cell"), St.Cell), 1.5);
		St.Strand = Pair(O, TEXT("strand"), St.Strand);
		St.Clear = Num(O, TEXT("clear"), St.Clear);
		St.Taper = Pair(O, TEXT("taper"), St.Taper);
		St.Holes = FMath::Clamp(Num(O, TEXT("holes"), St.Holes), 0.0, 0.6);
		St.Roots = int32(Num(O, TEXT("roots"), St.Roots));
		St.Glow = Num(O, TEXT("glow"), St.Glow);
		return St;
	}

	// Up made square to N (any square direction if it runs along N).
	FVector Tangent(const FVector& N, const FVector& Up)
	{
		FVector U = Up - N * FVector::DotProduct(Up, N);
		return U.Normalize() ? U : FVector::CrossProduct(N, FMath::Abs(N.Z) < 0.9f ? FVector::UpVector : FVector::ForwardVector).GetSafeNormal();
	}

	// A flora's detail levels: the defaults (FWWOGraftDetail::Default), each overridden by its recipe's "detail": a list,
	// one object per level, of any of its settings by name (strand_sides, leaf_lod, leaf_keep, kit_min, ...).
	void ReadDetail(const TSharedPtr<FJsonObject>& R, FWWOGraftDetail (&Out)[NumGraftLevels])
	{
		const TArray<TSharedPtr<FJsonValue>>* List = nullptr;
		const bool bList = R.IsValid() && R->TryGetArrayField(TEXT("detail"), List);
		for (int32 L = 0; L < NumGraftLevels; ++L)
		{
			FWWOGraftDetail& D = Out[L];
			D = FWWOGraftDetail::Default(L);
			const TSharedPtr<FJsonObject> O = bList && List->IsValidIndex(L) ? (*List)[L]->AsObject() : nullptr;
			if (!O.IsValid()) continue;
			auto Int = [&](const TCHAR* Field, int32& V) { double X; if (O->TryGetNumberField(Field, X)) V = int32(X); };
			auto Real = [&](const TCHAR* Field, float& V) { double X; if (O->TryGetNumberField(Field, X)) V = float(X); };
			auto Bool = [&](const TCHAR* Field, bool& V) { O->TryGetBoolField(Field, V); };
			Int(TEXT("strand_sides"), D.StrandSides); Int(TEXT("core_sides"), D.CoreSides); Int(TEXT("wind_sides"), D.WindSides);
			Int(TEXT("step"), D.Step); Int(TEXT("core_step"), D.CoreStep); Int(TEXT("rope_sides"), D.RopeSides); Bool(TEXT("strands"), D.bStrands);
			Int(TEXT("leaf_lod"), D.LeafLod); Real(TEXT("leaf_keep"), D.LeafKeep);
			Int(TEXT("twig_sides"), D.TwigSides); Int(TEXT("twig_step"), D.TwigStep); Int(TEXT("petiole_sides"), D.PetioleSides); Int(TEXT("petiole_step"), D.PetioleStep);
			Bool(TEXT("small"), D.bSmall); Int(TEXT("small_sides"), D.SmallSides); Int(TEXT("small_step"), D.SmallStep);
			Int(TEXT("crystal_rings"), D.CrystalRings); Int(TEXT("crystal_around"), D.CrystalAround);
			Int(TEXT("kit_lod"), D.KitLod); Real(TEXT("kit_min"), D.KitMin); Int(TEXT("kit_small_lod"), D.KitSmallLod); Real(TEXT("kit_small"), D.KitSmall);
			Int(TEXT("net_sides"), D.NetSides); Int(TEXT("net_thin_sides"), D.NetThinSides); Int(TEXT("net_step"), D.NetStep);
			Real(TEXT("net_min_radius"), D.NetMinRadius); Int(TEXT("knob_sides"), D.KnobSides); Int(TEXT("knob_rings"), D.KnobRings);
			Int(TEXT("plate_rows"), D.PlateRows); Int(TEXT("plate_cols"), D.PlateCols); Int(TEXT("pod_sides"), D.PodSides); Int(TEXT("pod_rings"), D.PodRings);
			Bool(TEXT("pod_stalk"), D.bPodStalk);
		}
	}
}

// --- Anchors --------------------------------------------------------------------------------------

const FWWOShellAnchorSet* UWWOBodyComponent::WornAnchors(int32 Slot) const
{
	if (!HasShells() || Slot < 0 || Slot > NumShellSlots) return nullptr;
	const USkeletalMesh* Mesh = Model->GetSkeletalMeshAsset();
	return WWOShellAnchors::Find(Slot == NumShellSlots ? SeamPart : WornParts[Slot], Mesh);
}

const FWWOShellPoint* UWWOBodyComponent::FindAnchorPoint(FName Name, bool* bOutSeam) const
{
	if (bOutSeam) *bOutSeam = false;
	for (int32 Slot = 0; Slot <= NumShellSlots; ++Slot)
	{
		const FWWOShellAnchorSet* Set = WornAnchors(Slot);
		if (const FWWOShellPoint* P = Set ? Set->Anchors.Find(Name) : nullptr)
		{
			if (bOutSeam) *bOutSeam = Slot == NumShellSlots;
			return P;
		}
	}
	return nullptr;
}

const FWWOShellPath* UWWOBodyComponent::FindPath(FName Name) const
{
	for (int32 Slot = 0; Slot < NumShellSlots; ++Slot)
	{
		const FWWOShellAnchorSet* Set = WornAnchors(Slot);
		if (const FWWOShellPath* P = Set ? Set->Paths.Find(Name) : nullptr) return P;
	}
	return nullptr;
}

FTransform UWWOBodyComponent::PointWorld(const FWWOShellPoint& Point) const
{
	if (!Model) return FTransform::Identity;
	// Against its bone as posed now; a point whose bone this mesh lacks stays where the reference pose had it.
	if (Point.BoneIndex != INDEX_NONE && Point.BoneIndex < Model->GetNumBones()) return Point.Local * Model->GetBoneTransform(Point.BoneIndex);
	return FTransform(FRotationMatrix::MakeFromXZ(Point.RefN, Point.RefUp).ToQuat(), Point.RefP) * Model->GetComponentTransform();
}

bool UWWOBodyComponent::FindAnchor(FName Name, FTransform& OutWorld, float* OutSize) const
{
	const FWWOShellPoint* P = FindAnchorPoint(Name);
	if (!P) return false;
	OutWorld = PointWorld(*P);
	if (OutSize) *OutSize = P->Size;
	return true;
}

bool UWWOBodyComponent::PathWorld(FName Name, TArray<FVector>& Out, bool bBridgeSeams) const
{
	Out.Reset();
	const FWWOShellPath* Path = FindPath(Name);
	if (!Path) return false;
	// The seams are where this slot's path hands over to the next one's: the ends can be a few cm apart, so
	// each runs on to the seam's own point.
	FTransform Seam;
	if (bBridgeSeams && !Path->FromSeam.IsNone() && FindAnchor(Path->FromSeam, Seam)) Out.Add(Seam.GetLocation());
	for (const FWWOShellPoint& P : Path->Points) Out.Add(PointWorld(P).GetLocation());
	if (Path->bClosed) Out.Add(PointWorld(Path->Points[0]).GetLocation());
	if (bBridgeSeams && !Path->ToSeam.IsNone() && FindAnchor(Path->ToSeam, Seam)) Out.Add(Seam.GetLocation());
	return true;
}

// --- Building (on a worker thread) ----------------------------------------------------------------

namespace
{
	// What a build reads, gathered on the game thread (the worn parts' anchor sets stay put for the run).
	struct FAnchorInputs
	{
		FName Flora, Grenade;
		TArray<FName> Ids;
		FName Parts[UWWOBodyComponent::NumShellSlots + 1]; // the worn parts, then the seams' (the bare skeleton)
		const FWWOShellAnchorSet* Sets[UWWOBodyComponent::NumShellSlots + 1] = {}; // their anchor sets (found by the build)
		const USkeletalMesh* Mesh = nullptr;
		// The meshes it copies: loaded by the game thread first (FWWOAnchorGraftCache), and kept until the build is done.
		TArray<FString> KitPaths;
		TArray<const UStaticMesh*> KitMeshes, LeafMeshes;
		TSharedPtr<FJsonObject> Recipe;
		bool bJoints = true;
		FWWOGraftDetail Detail[NumGraftLevels];
		FString Seed;  // the kit (grenade|ids): the same look for the same kit
		FString Key;   // the cache's: flora, kit, worn parts
		FString Asker; // the first body that asked (for the log)
	};

	// What a build makes: per bone, a mesh description for each level; the joint sections; the log's numbers.
	struct FAnchorBuilt
	{
		TArray<FName> Bones;
		TArray<TArray<FMeshDescription>> Levels; // per bone: NumGraftLevels
		TArray<FIntVector> BoneTris;             // per bone: triangles at each level
		TArray<TArray<int32>> Sections;          // per bone: the materials (FWWOVineMesh sections) its mesh has, in slot order
		TSharedPtr<FWWOVineJoints> Joints;
		int32 Tris[NumGraftLevels] = { 0, 0, 0 };
		double WorkMs = 0.0;
		FString Summary;
	};

	FName SlotName(int32 Section) { return FName(*FString::Printf(TEXT("S%d"), Section)); }

	// A bone's mesh is made into a static mesh on the game thread, in one go: one too big for a frame's budget is cut
	// into parts of about this many triangles (at the closest level; each level is cut the same number of ways).
	constexpr int32 MaxPartTris = 4000;

	// Triangles [Part / Parts, (Part + 1) / Parts) of a section, with the vertices they use.
	void CutSection(const FWWOVineMesh::FSection& Src, int32 Part, int32 Parts, FWWOVineMesh::FSection& Dst)
	{
		const int32 NumT = Src.Tris.Num() / 3;
		const int32 T0 = NumT * Part / Parts, T1 = NumT * (Part + 1) / Parts;
		TArray<int32> Map;
		Map.Init(INDEX_NONE, Src.V.Num());
		const bool bUV1 = Src.UV1.Num() == Src.V.Num(), bColor = Src.C.Num() == Src.V.Num();
		for (int32 k = T0 * 3; k < T1 * 3; ++k)
		{
			const int32 v = Src.Tris[k];
			if (Map[v] == INDEX_NONE)
			{
				Map[v] = Dst.V.Num();
				Dst.V.Add(Src.V[v]);
				Dst.N.Add(Src.N[v]);
				Dst.T.Add(Src.T[v]);
				Dst.UV.Add(Src.UV[v]);
				if (bUV1) Dst.UV1.Add(Src.UV1[v]);
				if (bColor) Dst.C.Add(Src.C[v]);
			}
			Dst.Tris.Add(Map[v]);
		}
	}

	// One level of a bone's mesh as a mesh description: a polygon group per material in Used (the sections any level of
	// this mesh has), in that order, its own vertices for each vertex as built. A material this level has no triangles
	// in gets one degenerate triangle, so every level has every group (the mesh's sections and its material slots then
	// line up, by name and by order alike, at every LOD).
	void Describe(const FWWOVineMesh& M, const FVector& Fallback, const TArray<int32>& Used, FMeshDescription& MD)
	{
		FStaticMeshAttributes Attr(MD);
		Attr.Register();
		TVertexAttributesRef<FVector3f> Pos = Attr.GetVertexPositions();
		TVertexInstanceAttributesRef<FVector3f> Nrm = Attr.GetVertexInstanceNormals();
		TVertexInstanceAttributesRef<FVector3f> Tan = Attr.GetVertexInstanceTangents();
		TVertexInstanceAttributesRef<float> Sign = Attr.GetVertexInstanceBinormalSigns();
		TVertexInstanceAttributesRef<FVector4f> Col = Attr.GetVertexInstanceColors();
		TVertexInstanceAttributesRef<FVector2f> UVs = Attr.GetVertexInstanceUVs();
		UVs.SetNumChannels(2);
		TPolygonGroupAttributesRef<FName> Slots = Attr.GetPolygonGroupMaterialSlotNames();
		int32 NumV = 0, NumT = 0;
		for (const int32 s : Used) { NumV += M.S[s].V.Num() + 3; NumT += M.S[s].Tris.Num() / 3 + 1; }
		MD.ReserveNewVertices(NumV);
		MD.ReserveNewVertexInstances(NumV);
		MD.ReserveNewTriangles(NumT);
		MD.ReserveNewPolygons(NumT);
		TArray<FVertexInstanceID> Ids;
		for (const int32 s : Used)
		{
			const FWWOVineMesh::FSection& Sec = M.S[s];
			const FPolygonGroupID Group = MD.CreatePolygonGroup();
			Slots[Group] = SlotName(s);
			if (Sec.Tris.Num() == 0)
			{
				TArray<FVertexInstanceID, TFixedAllocator<3>> Tri;
				for (int32 k = 0; k < 3; ++k)
				{
					const FVertexID Vx = MD.CreateVertex();
					Pos[Vx] = FVector3f(Fallback);
					const FVertexInstanceID I = MD.CreateVertexInstance(Vx);
					Nrm[I] = FVector3f(0.f, 0.f, 1.f);
					Tan[I] = FVector3f(1.f, 0.f, 0.f);
					Sign[I] = 1.f;
					Col[I] = FVector4f(1.f, 1.f, 1.f, 1.f);
					Tri.Add(I);
				}
				MD.CreateTriangle(Group, Tri);
				continue;
			}
			Ids.Reset(Sec.V.Num());
			const bool bUV1 = Sec.UV1.Num() == Sec.V.Num(), bColor = Sec.C.Num() == Sec.V.Num();
			for (int32 v = 0; v < Sec.V.Num(); ++v)
			{
				const FVertexID Vx = MD.CreateVertex();
				Pos[Vx] = FVector3f(Sec.V[v]);
				const FVertexInstanceID I = MD.CreateVertexInstance(Vx);
				Nrm[I] = FVector3f(Sec.N[v]);
				Tan[I] = FVector3f(Sec.T[v].TangentX);
				Sign[I] = Sec.T[v].bFlipTangentY ? -1.f : 1.f;
				UVs.Set(I, 0, FVector2f(Sec.UV[v]));
				UVs.Set(I, 1, bUV1 ? FVector2f(Sec.UV1[v]) : FVector2f::ZeroVector);
				// (Stored linear; the build writes it back as sRGB bytes, so these come out as the raw bytes given.)
				Col[I] = bColor ? FVector4f(FLinearColor(Sec.C[v])) : FVector4f(1.f, 1.f, 1.f, 1.f);
				Ids.Add(I);
			}
			for (int32 t = 0; t + 2 < Sec.Tris.Num(); t += 3)
			{
				MD.CreateTriangle(Group, { Ids[Sec.Tris[t]], Ids[Sec.Tris[t + 1]], Ids[Sec.Tris[t + 2]] });
			}
		}
	}

	// The whole build: every piece of the recipe for this kit on these parts, at every level. Runs on a worker thread:
	// it reads only In: the worn parts' anchor files (read here the first time a part is worn) and the copied meshes, which
	// the game thread loaded and keeps loaded until it is done.
	TSharedPtr<FAnchorBuilt> BuildGeometry(FAnchorInputs& In)
	{
		const double Started = FPlatformTime::Seconds();
		TSharedPtr<FAnchorBuilt> Out = MakeShared<FAnchorBuilt>();
		// The worn parts' anchor sets (a part's file is read the first time it is worn) and the copied meshes' shapes.
		for (int32 Slot = 0; Slot <= UWWOBodyComponent::NumShellSlots; ++Slot) In.Sets[Slot] = WWOShellAnchors::Find(In.Parts[Slot], In.Mesh);
		FWWOVineGraft::ReadLeaves(In.LeafMeshes);
		for (int32 k = 0; k < In.KitPaths.Num(); ++k) FWWOVineGraft::ReadMesh(In.KitPaths[k], In.KitMeshes.IsValidIndex(k) ? In.KitMeshes[k] : nullptr);
		const TSharedPtr<FJsonObject>& R = In.Recipe;
		const USkeletalMesh* Mesh = In.Mesh;
		constexpr int32 NumSlots = UWWOBodyComponent::NumShellSlots;
		auto FindAnchorPoint = [&](FName Name) -> const FWWOShellPoint*
		{
			for (int32 Slot = 0; Slot <= NumSlots; ++Slot)
			{
				if (const FWWOShellPoint* P = In.Sets[Slot] ? In.Sets[Slot]->Anchors.Find(Name) : nullptr) return P;
			}
			return nullptr;
		};
		auto FindPath = [&](FName Name) -> const FWWOShellPath*
		{
			for (int32 Slot = 0; Slot < NumSlots; ++Slot)
			{
				if (const FWWOShellPath* P = In.Sets[Slot] ? In.Sets[Slot]->Paths.Find(Name) : nullptr) return P;
			}
			return nullptr;
		};
		// The same look every time for the same kit (as RebuildGrafts' procedural grafts).
		FRandomStream Random(int32(GetTypeHash(In.Seed)));
		// One mesh per bone, built in the robot mesh's space in the reference pose (where the anchors are, and
		// where the builder's "down" is the body's down) and hung on that bone. Plates and pods: a builder per bone and
		// level, each drawing from its level's stream, which every piece starts again from the same seed, so all levels
		// get the same bumps where they have the same points.
		FRandomStream LevelRandom[NumGraftLevels];
		TMap<FName, TUniquePtr<FWWOGraftBuilder>> Builders[NumGraftLevels];
		auto BuilderFor = [&](FName Bone, int32 L) -> FWWOGraftBuilder&
		{
			TUniquePtr<FWWOGraftBuilder>& B = Builders[L].FindOrAdd(Bone);
			if (!B)
			{
				B = MakeUnique<FWWOGraftBuilder>(FVector::ZeroVector, FLinearColor::White, WWO::M, LevelRandom[L]);
				B->VineColor = FLinearColor(0.2f, 0.13f, 0.07f); // the seed pods' stalks (as BarkGrafts)
				const FWWOGraftDetail& D = In.Detail[L];
				B->PlateRows = D.PlateRows;
				B->PlateCols = D.PlateCols;
				B->PodSides = D.PodSides;
				B->PodRings = D.PodRings;
				B->bPodStalk = D.bPodStalk;
				B->TubeSides = L == 0 ? 5 : 3;
			}
			return *B;
		};
		int32 Pieces = 0, Missing = 0;
		// The worn parts' surface (their wraps' samples and anchors, FWWOShellSurface): made on the first piece that is
		// walked over it or seated on it.
		TUniquePtr<FWWOShellSurface> Surface;
		auto SurfaceNow = [&]() -> const FWWOShellSurface&
		{
			if (!Surface)
			{
				Surface = MakeUnique<FWWOShellSurface>();
				for (int32 Slot = 0; Slot < NumSlots; ++Slot)
				{
					const FWWOShellAnchorSet* Set = In.Sets[Slot];
					if (!Set) continue;
					for (const FWWOShellSurface::FSample& P : Set->Surface) Surface->Add(P);
					for (const TPair<FName, FWWOShellPoint>& It : Set->Anchors) Surface->Add(It.Value);
				}
				Surface->Finish();
			}
			return *Surface;
		};
		// Every worn part's obstacles (thin bits standing out of it, such as the scout head's antenna): no piece may enter
		// one (Grow cm bigger), whatever the recipe says.
		TArray<const FWWOShellVolume*> Obstacles;
		for (int32 Slot = 0; Slot < NumSlots; ++Slot)
		{
			if (const FWWOShellAnchorSet* Set = In.Sets[Slot]) for (const TPair<FName, FWWOShellVolume>& It : Set->Obstacles) Obstacles.Add(&It.Value);
		}
		auto FindVolume = [&](FName Name) -> const FWWOShellVolume*
		{
			for (int32 Slot = 0; Slot < NumSlots; ++Slot)
			{
				if (const FWWOShellVolume* V = In.Sets[Slot] ? In.Sets[Slot]->Volumes.Find(Name) : nullptr) return V;
			}
			return nullptr;
		};
		auto Obstacle = [&](const FVector& P, float Grow) -> const FWWOShellVolume*
		{
			for (const FWWOShellVolume* V : Obstacles) if (V->Contains(P, Grow)) return V;
			return nullptr;
		};
		int32 ObstaclePushed = 0, ObstacleLeftOut = 0, VineInObstacles = 0;
		int32 Seated = 0;
		float SeatGapBefore = 0.f, SeatGapAfter = 0.f; // the seated pieces' mean gaps under them, summed (cm)
		// One piece at one point. Sign is -1 on the right side of a "{s}" pair: its across offset is mirrored.
		// seat: its offset's across and up are walked over the surface, and it is then fitted to the surface under it
		// (FWWOShellSurface::Seat: moved in or out, leaning with it), its offset's out the gap left under it.
		auto Place = [&](const FWWOShellPoint& At, float Scale, float Sign, const TSharedPtr<FJsonObject>& O)
		{
			const FVector Off = WWOConfig::Vec(O, TEXT("offset"));
			const bool bSeat = Flag(O, TEXT("seat"), false);
			FVector P = At.RefP, N = At.RefN, Up = At.RefUp;
			FName Bone = At.Bone;
			if (bSeat && (Off.Y != 0.0 || Off.Z != 0.0))
			{
				const FWWOShellSurface& Sf = SurfaceNow();
				FName Walked = Bone;
				if (Off.Z != 0.0) Sf.Walk(P, N, Tangent(N, Up), float(Off.Z), &Walked);
				if (Off.Y != 0.0) Sf.Walk(P, N, FVector::CrossProduct(Tangent(N, Up), N), float(Off.Y) * Sign, &Walked);
				Up = Tangent(N, Up);
				if (FVector::Dist(P, At.RefP) > 0.5f) Bone = Walked;
			}
			const FVector Across = FVector::CrossProduct(Up, N); // the anchor frame's Y (up x out)
			FVector Center = bSeat ? P : At.RefP + At.RefN * Off.X + Across * Off.Y * Sign + At.RefUp * Off.Z;
			FString Piece, AlongWord(TEXT("down"));
			O->TryGetStringField(TEXT("piece"), Piece);
			O->TryGetStringField(TEXT("along"), AlongWord);
			FVector Along = AlongWord == TEXT("up") ? Up : AlongWord == TEXT("across") ? Across * Sign : -Up;
			const float Cm = 1.f / WWO::M; // the builder's sizes are meters
			const bool bPlate = Piece == TEXT("plate"), bPod = Piece == TEXT("seed_pod");
			if (!bPlate && !bPod)
			{
				UE_LOG(LogWWO, Warning, TEXT("Graft recipes: unknown piece '%s'"), *Piece);
				return;
			}
			const float Length = Num(O, TEXT("length"), bPlate ? 12.0 : 10.0) * Scale, Width = Num(O, TEXT("width"), bPlate ? 8.0 : 3.5) * Scale;
			float Curve = FMath::Max(Num(O, TEXT("curve"), 30.0) * Scale, 1.0);
			const float Stalk = Num(O, TEXT("stalk"), 4.0);
			// The piece's underside (cm), to seat it and to keep it off the obstacles; X: how far across a plate each is.
			TArray<FVector> Under;
			TArray<float> X;
			const FVector Back = FVector(N.X, N.Y, 0.f).GetSafeNormal(UE_SMALL_NUMBER, N); // a pod's way out: level
			auto Underside = [&](float Bend)
			{
				Under.Reset();
				X.Reset();
				if (bPlate)
				{
					// As FWWOGraftBuilder::Plate lays its inside (without its lift): rows along it, bent round its length.
					const FVector Side = FVector::CrossProduct(N, Along).GetSafeNormal();
					for (int32 Row = 0; Row <= 4; ++Row)
					{
						const float V = Row / 4.f - 0.5f;
						const float Taper = 1.f - FMath::Pow(FMath::Abs(V * 2.f), 2.4f) * (V > 0.f ? 0.78f : 0.6f);
						for (int32 Col = 0; Col <= 4; ++Col)
						{
							const float A = (Col / 2.f - 1.f) * Width * 0.5f * Taper / Bend;
							Under.Add(Center + Along * V * Length + Side * FMath::Sin(A) * Bend + N * (FMath::Cos(A) - 1.f) * Bend);
							X.Add(FMath::Sin(A) * Bend);
						}
					}
				}
				else
				{
					// Hanging straight down from its top: the stalk's top, then the back of its body (toward the body).
					Under.Add(Center);
					for (int32 k = 1; k <= 5; ++k)
					{
						const float T = k / 6.f;
						const float R = Width * FMath::Pow(FMath::Max(FMath::Sin(UE_PI * FMath::Pow(T, 1.35f)), 0.f), 0.7f);
						Under.Add(Center - FVector::UpVector * (Stalk + T * Length) - Back * R);
					}
				}
			};
			Underside(Curve);
			FVector Hang = FVector::ZeroVector;
			FString What; // (for the log: its anchor or path, its offset)
			if (!O->TryGetStringField(TEXT("anchor"), What)) O->TryGetStringField(TEXT("path"), What);
			What = FString::Printf(TEXT("%s %s [%g %g %g]"), *Piece, *What.Replace(TEXT("{s}"), Sign < 0.f ? TEXT("r") : TEXT("l")), Off.X, Off.Y, Off.Z);
			if (!bSeat && UE_LOG_ACTIVE(LogWWO, Verbose))
			{
				// (How well an unseated piece fits, to compare.)
				FWWOShellSurface::FSeat Fit;
				if (SurfaceNow().Seat(Under, Center, bPod ? Back : N, 0.f, 0.f, FVector::ZeroVector, Fit))
				{
					UE_LOG(LogWWO, Verbose, TEXT("Anchor fit: %s (not seated): gap mean %.1f (%.1f..%.1f) cm"), *What, Fit.MeanBefore, Fit.MinBefore, Fit.MaxBefore);
				}
			}
			if (bSeat)
			{
				// A plate leans any way (up to 25 degrees); a pod swings only toward or away from the body (up to 30), from its top.
				const FVector Pivot = Center;
				FWWOShellSurface::FSeat Fit;
				auto Fitted = [&]() { return SurfaceNow().Seat(Under, Pivot, bPod ? Back : N, FMath::Max(float(Off.X), 0.f), bPod ? 30.f : 25.f,
					bPod ? FVector::CrossProduct(FVector::UpVector, Back).GetSafeNormal() : FVector::ZeroVector, Fit); };
				bool bFitted = Fitted();
				const float GapFirst = Fit.MeanBefore, MinFirst = Fit.MinBefore, MaxFirst = Fit.MaxBefore;
				if (bFitted && bPlate)
				{
					// A plate also bends to the surface across it: the gaps' rise toward its edges (q x^2) says how much
					// more (or less) it should curl (its inside drops x^2 / 2 Curve), within half to three times the recipe's.
					double Sx = 0, Sy = 0, Sxx = 0, Sxy = 0;
					int32 Count = 0;
					for (int32 i = 0; i < Under.Num(); ++i)
					{
						if (FMath::IsNaN(Fit.Gaps[i])) continue;
						const double Q = X[i] * X[i];
						Sx += Q; Sy += Fit.Gaps[i]; Sxx += Q * Q; Sxy += Q * Fit.Gaps[i];
						++Count;
					}
					const double Den = Count * Sxx - Sx * Sx;
					if (Count > 4 && Den > 1e-6)
					{
						const double Q = (Count * Sxy - Sx * Sy) / Den;
						const float Bend = FMath::Clamp(1.f / FMath::Max(1.f / Curve + 2.f * float(Q), 1e-3f), Curve * 0.5f, Curve * 3.f);
						if (FMath::Abs(Bend - Curve) > Curve * 0.05f)
						{
							Curve = Bend;
							Underside(Curve);
							bFitted = Fitted();
						}
					}
				}
				if (bFitted)
				{
					for (FVector& U : Under) U = Fit.Apply(U, Pivot);
					Center = Fit.Apply(Center, Pivot);
					N = Fit.Turn.RotateVector(N);
					Along = Fit.Turn.RotateVector(Along);
					if (bPod) Hang = Fit.Turn.RotateVector(-FVector::UpVector);
					++Seated;
					SeatGapBefore += GapFirst;
					SeatGapAfter += Fit.MeanAfter;
					UE_LOG(LogWWO, Verbose, TEXT("Anchor seat: %s: gap mean %.1f (%.1f..%.1f) -> %.1f (%.1f..%.1f) cm, tilt %.0f deg, curve %.1f"), *What,
						GapFirst, MinFirst, MaxFirst, Fit.MeanAfter, Fit.MinAfter, Fit.MaxAfter, Fit.TiltDeg, Curve);
				}
			}
			for (const FVector& U : Under)
			{
				if (Obstacle(U, 1.f)) { ++ObstacleLeftOut; return; }
			}
			const int32 PieceSeed = int32(Random.GetUnsignedInt() & 0x7fffffff);
			for (int32 L = 0; L < NumGraftLevels; ++L)
			{
				LevelRandom[L].Initialize(PieceSeed);
				FWWOGraftBuilder& B = BuilderFor(Bone, L);
				if (bPlate)
				{
					B.Plate(Center, N, Along, Length * Cm, Width * Cm, Curve * Cm, Num(O, TEXT("thickness"), 2.0) * Cm, Num(O, TEXT("lift"), 0.0) * Cm,
						Flag(O, TEXT("cracked"), true));
				}
				else
				{
					B.SeedPod(Center, Length * Cm, Width * Cm, Stalk * Cm, Hang);
				}
			}
			++Pieces;
		};
		// VoltVine's pieces (FWWOVineGraft): made on the first one asked for.
		TUniquePtr<FWWOVineGraft> Vines;
		auto VinesNow = [&]() -> FWWOVineGraft&
		{
			if (!Vines)
			{
				Vines = MakeUnique<FWWOVineGraft>(Mesh, Random, In.bJoints, In.Detail);
				if (Obstacles.Num() > 0) Vines->Blocked = [&](const FVector& P) { return Obstacle(P, 0.5f) != nullptr; };
			}
			return *Vines;
		};
		// A vine's line: its 'paths' one after another (each over 'range' [from, to] of its length; from > to runs it
		// backward), each next one joined to the last by a short lifted bridge (across a seam, where a Core path's end
		// and an Arms or Legs path's start lie a few cm apart on their own parts). A path's offset_cm moves its line that far
		// sideways over the surface, a lane beside it, toward 'toward' ('front', 'back', 'up', 'down', 'outer'; default
		// 'back'): round the limb from a line down its outside. False if a path is missing.
		auto VineLine = [&](const TSharedPtr<FJsonObject>& O, const TFunctionRef<FString(const FString&)> Named, float Sign, FWWOVineLine& Line)
		{
			const TArray<TSharedPtr<FJsonValue>>* Paths = nullptr;
			if (!O->TryGetArrayField(TEXT("paths"), Paths)) return false;
			for (const TSharedPtr<FJsonValue>& V : *Paths)
			{
				const TSharedPtr<FJsonObject> E = V->AsObject();
				FString Name;
				if (!E.IsValid() || !E->TryGetStringField(TEXT("path"), Name)) return false;
				const FWWOShellPath* Path = FindPath(FName(*Named(Name)));
				if (!Path) return false;
				const FVector2D Range = Pair(E, TEXT("range"), FVector2D(0.0, 1.0));
				const float Thick = Num(E, TEXT("thickness"), 1.0);
				const FVector2D LeafScale = Pair(E, TEXT("leaf_scale"), FVector2D(1.0, 1.0));
				const float Leafy = Flag(E, TEXT("leaves"), true) ? 1.f : 0.f;
				const float Lane = Num(E, TEXT("offset_cm"), 0.0);
				FString Toward(TEXT("back"));
				E->TryGetStringField(TEXT("toward"), Toward);
				const FVector LaneWay = Word(Toward, Sign);
				const TArray<FWWOShellPoint>& Pts = Path->Points;
				TArray<float> At;
				At.Add(0.f);
				for (int32 k = 1; k < Pts.Num(); ++k) At.Add(At.Last() + FVector::Dist(Pts[k - 1].RefP, Pts[k].RefP));
				const float Total = At.Last();
				const float S0 = FMath::Clamp(float(Range.X), 0.f, 1.f) * Total, S1 = FMath::Clamp(float(Range.Y), 0.f, 1.f) * Total;
				TArray<float> Walk = { S0 };
				if (S0 <= S1) { for (int32 k = 0; k < Pts.Num(); ++k) if (At[k] > S0 && At[k] < S1) Walk.Add(At[k]); }
				else { for (int32 k = Pts.Num() - 1; k >= 0; --k) if (At[k] < S0 && At[k] > S1) Walk.Add(At[k]); }
				Walk.Add(S1);
				for (int32 w = 0; w < Walk.Num(); ++w)
				{
					const float S = Walk[w];
					int32 k = 0;
					while (k + 2 < Pts.Num() && At[k + 1] < S) ++k;
					const float Len = At[k + 1] - At[k];
					const float T = Len > UE_SMALL_NUMBER ? FMath::Clamp((S - At[k]) / Len, 0.f, 1.f) : 0.f;
					FVector P = FMath::Lerp(Pts[k].RefP, Pts[k + 1].RefP, T);
					FVector N = FMath::Lerp(Pts[k].RefN, Pts[k + 1].RefN, T).GetSafeNormal();
					FName Bone = T < 0.5f ? Pts[k].Bone : Pts[k + 1].Bone;
					if (Lane != 0.f)
					{
						const FVector Side = FVector::CrossProduct((Pts[k + 1].RefP - Pts[k].RefP).GetSafeNormal(), N);
						SurfaceNow().Walk(P, N, FVector::DotProduct(Side, LaneWay) < 0.0 ? -Side : Side, Lane, &Bone);
					}
					const float F = Walk.Num() > 1 ? float(w) / (Walk.Num() - 1) : 0.f;
					const float LS = FMath::Lerp(float(LeafScale.X), float(LeafScale.Y), F);
					if (w == 0 && Line.Num() > 0)
					{
						// The bridge from the last path's end: straight, lifted in the middle by about the sag of a chord
						// over a body ~10 cm round, so it does not cut into the armour between.
						const FVector From = Line.P.Last(), FromN = Line.N.Last();
						const float Gap = FVector::Dist(From, P);
						const int32 Steps = FMath::CeilToInt(Gap / 1.f);
						for (int32 j = 1; j < Steps; ++j)
						{
							const float B = float(j) / Steps;
							const FVector Nb = FMath::Lerp(FromN, N, B).GetSafeNormal();
							const float Lift = 4.f * B * (1.f - B) * FMath::Min(Gap * Gap / 80.f, 2.5f);
							Line.Add(FMath::Lerp(From, P, B) + Nb * Lift, Nb, B < 0.5f ? Line.Bone.Last() : Bone, FMath::Lerp(Line.Thick.Last(), Thick, B),
								FMath::Lerp(Line.LeafScale.Last(), LS, B), FMath::Min(Line.Leafy.Last(), Leafy));
						}
					}
					if (Line.Num() > 0 && FVector::Dist(Line.P.Last(), P) < 0.05f) continue;
					Line.Add(P, N, Bone, Thick, LS, Leafy);
				}
			}
			return Line.Num() >= 2;
		};
		// One VoltVine piece, on one side (Sign -1: the right of a "{s}" pair).
		auto PlaceVine = [&](const TSharedPtr<FJsonObject>& O, const FString& Piece, const TFunctionRef<FString(const FString&)> Named, float Sign)
		{
			FWWOVineGraft& G = VinesNow();
			if (Piece == TEXT("vine") || Piece == TEXT("bone_wrap"))
			{
				FWWOVineLine Line;
				if (Piece == TEXT("vine"))
				{
					if (!VineLine(O, Named, Sign, Line)) { ++Missing; return; }
				}
				else
				{
					FString Bone, ToBone;
					O->TryGetStringField(TEXT("bone"), Bone);
					O->TryGetStringField(TEXT("to_bone"), ToBone);
					const FName B(*Named(Bone)), To(*Named(ToBone));
					if (Mesh->GetRefSkeleton().FindBoneIndex(B) == INDEX_NONE || Mesh->GetRefSkeleton().FindBoneIndex(To) == INDEX_NONE) { ++Missing; return; }
					const FVector2D Range = Pair(O, TEXT("range"), FVector2D(0.0, 1.0));
					FWWOVineGraft::BoneWrap(Mesh, B, To, Range.X, Range.Y, Num(O, TEXT("turns"), 1.0), Num(O, TEXT("phase"), 0.0),
						Pair(O, TEXT("radii"), FVector2D(3.0, 3.0)), FVector(Sign, 0.0, 0.0), Line);
				}
				for (const FVector& P : Line.P) VineInObstacles += Obstacle(P, 1.f) ? 1 : 0; // (not avoided yet: counted)
				G.Vine(FWWOVineGraft::Resample(Line, 1.2f), VineStyle(O, Sign));
				return;
			}
			FString AnchorName;
			O->TryGetStringField(TEXT("anchor"), AnchorName);
			const FWWOShellPoint* At = FindAnchorPoint(FName(*Named(AnchorName)));
			if (!At) { ++Missing; return; }
			const FVector Across = FVector::CrossProduct(At->RefUp, At->RefN);
			const FVector Off = WWOConfig::Vec(O, TEXT("offset"));
			const FVector Center = At->RefP + At->RefN * Off.X + Across * Off.Y * Sign + At->RefUp * Off.Z;
			if (Piece == TEXT("crystal"))
			{
				// lean: [across, up] added to the way out, so a row of pods fans a little.
				const FVector2D Lean = Pair(O, TEXT("lean"), FVector2D(0.0, 0.0));
				G.Crystal(At->Bone, Center, (At->RefN + Across * Sign * Lean.X + At->RefUp * Lean.Y).GetSafeNormal(), Num(O, TEXT("size"), 0.4));
			}
			else // leaf_fan: outward (the robot's side) and a little down, fanned front to back
			{
				G.LeafFan(At->Bone, Center, FVector(Sign, 0.0, 0.0), FVector(0.0, 1.0, 0.0), int32(Num(O, TEXT("count"), 5.0)),
					Pair(O, TEXT("size"), FVector2D(11.0, 14.0)), Num(O, TEXT("down"), 0.25));
			}
		};
		// VeilSpore's pieces: static meshes from the flora's 'kit' folder, placed over the worn parts' surface (their
		// wraps' samples and anchors, FWWOShellSurface), and its mycelium, grown once every cap is placed (it glows
		// near them).
		FString Kit, KitSectionName;
		R->TryGetStringField(TEXT("kit"), Kit);
		// kit_section: the material the kit's pieces are drawn in: 'fungus' (VeilSpore's atlas, the default) or 'bark' (IronBark's kit atlas).
		R->TryGetStringField(TEXT("kit_section"), KitSectionName);
		const FWWOVineMesh::ESection KitSection = KitSectionName == TEXT("bark") ? FWWOVineMesh::KitBark : FWWOVineMesh::Fungus;
		TArray<FVector4> Glow;           // each cap's gills (where, how far their light reaches the mycelium)
		TArray<TFunction<void()>> Later; // the mycelium
		int32 KeptOff = 0;               // pieces left out to keep the face clear
		// One kit piece (or one per spot along a path), on one side.
		auto PlaceMesh = [&](const TSharedPtr<FJsonObject>& O, const TFunctionRef<FString(const FString&)> Named, float Sign)
		{
			FString MeshName, AnchorName, PathName, UpMode;
			// Which mesh: 'mesh' (one), or 'meshes' (a list: each spot takes one, by its seed); a grid's 'crack' list goes
			// to its middle column (IronBark's scales with a glowing crack down them).
			struct FVariant { const FWWOLeafShape* Shape; const FWWOLeafShape* Shapes[NumGraftLevels]; const FWWOLeafShape* SmallShapes[NumGraftLevels]; };
			auto Resolve = [&](const FString& Name, TArray<FVariant>& Out)
			{
				const FString ShapePath = FString::Printf(TEXT("%s/%s.%s"), *Kit, *Name, *Name);
				FVariant V;
				V.Shape = Kit.IsEmpty() ? nullptr : FWWOVineGraft::MeshShape(ShapePath);
				for (int32 L = 0; L < NumGraftLevels; ++L)
				{
					V.Shapes[L] = V.Shape ? FWWOVineGraft::MeshShape(ShapePath, In.Detail[L].KitLod) : nullptr;
					V.SmallShapes[L] = V.Shape ? FWWOVineGraft::MeshShape(ShapePath, In.Detail[L].KitSmallLod) : nullptr;
				}
				if (V.Shape) { Out.Add(V); return; }
				UE_LOG(LogWWO, Warning, TEXT("Graft recipes: no kit mesh '%s' in '%s'"), *Name, *Kit);
				++Missing;
			};
			auto ResolveList = [&](const TSharedPtr<FJsonObject>& From, const TCHAR* Field, TArray<FVariant>& Out)
			{
				const TArray<TSharedPtr<FJsonValue>>* Names = nullptr;
				if (From.IsValid() && From->TryGetArrayField(Field, Names)) for (const TSharedPtr<FJsonValue>& V : *Names) Resolve(V->AsString(), Out);
			};
			const TSharedPtr<FJsonObject> Grid = Object(O, TEXT("grid"));
			TArray<FVariant> Plain, Cracked;
			if (O->TryGetStringField(TEXT("mesh"), MeshName)) Resolve(MeshName, Plain);
			ResolveList(O, TEXT("meshes"), Plain);
			ResolveList(Grid, TEXT("crack"), Cracked);
			if (Plain.IsEmpty()) return;
			// Where: an anchor, or along a path (every spacing_cm over 'range', or once at 'at').
			TArray<FWWOShellPoint> Spots;
			TArray<bool> SpotCrack; // (a grid's: its middle column)
			float SlideMax = 0.f, SizeScale = 1.f;
			const bool bPath = !O->TryGetStringField(TEXT("anchor"), AnchorName);
			if (!bPath)
			{
				const FWWOShellPoint* P = FindAnchorPoint(FName(*Named(AnchorName)));
				if (!P) { ++Missing; return; }
				Spots.Add(*P);
				SlideMax = P->Size;
				// grid: rows of pieces laid over the surface round the anchor like shingles (the anchor at the middle): 'cols'
				// (pieces in each row, top row first: a list, or one number with 'rows'), step_up and step_across (cm between
				// rows and between pieces). Each row is centred, so rows of odd and even counts overlap like bricks.
				if (Grid.IsValid())
				{
					TArray<int32> Cols;
					const TArray<TSharedPtr<FJsonValue>>* ColList = nullptr;
					if (Grid->TryGetArrayField(TEXT("cols"), ColList)) for (const TSharedPtr<FJsonValue>& V : *ColList) Cols.Add(FMath::Clamp(int32(V->AsNumber()), 0, 12));
					else Cols.Init(FMath::Clamp(int32(Num(Grid, TEXT("cols"), 3.0)), 1, 12), FMath::Clamp(int32(Num(Grid, TEXT("rows"), 3.0)), 1, 16));
					const float StepUp = Num(Grid, TEXT("step_up"), 5.0), StepAcross = Num(Grid, TEXT("step_across"), 4.0);
					FString GridUpMode;
					O->TryGetStringField(TEXT("up"), GridUpMode);
					const FVector GridUp = GridUpMode == TEXT("world") && FMath::Abs(P->RefN.Z) < 0.9f ? FVector::UpVector : P->RefUp;
					const FWWOShellSurface& Sf = SurfaceNow();
					Spots.Reset();
					for (int32 r = 0; r < Cols.Num(); ++r)
					{
						for (int32 c = 0; c < Cols[r]; ++c)
						{
							FWWOShellPoint Spot = *P;
							FName Walked = P->Bone;
							const float UpCm = ((Cols.Num() - 1) * 0.5f - r) * StepUp, AcrossCm = (c - (Cols[r] - 1) * 0.5f) * StepAcross;
							if (UpCm != 0.f) Sf.Walk(Spot.RefP, Spot.RefN, Tangent(Spot.RefN, GridUp), UpCm, &Walked);
							if (AcrossCm != 0.f) Sf.Walk(Spot.RefP, Spot.RefN, FVector::CrossProduct(Tangent(Spot.RefN, GridUp), Spot.RefN), AcrossCm * Sign, &Walked);
							if (FVector::Dist(Spot.RefP, P->RefP) > 0.5f) Spot.Bone = Walked;
							Spots.Add(Spot);
							SpotCrack.Add(c == (Cols[r] - 1) / 2);
						}
					}
					SlideMax = StepAcross * 0.3f;
				}
				if (Flag(O, TEXT("scale_by_size"), false)) SizeScale = FMath::Clamp(P->Size / float(Num(O, TEXT("size_ref"), 5.0)), 0.6f, 1.6f);
			}
			else
			{
				O->TryGetStringField(TEXT("path"), PathName);
				const FWWOShellPath* Path = FindPath(FName(*Named(PathName)));
				if (!Path) { ++Missing; return; }
				double At = 0.0;
				const float Spacing = Num(O, TEXT("spacing_cm"), 8.0);
				if (O->TryGetNumberField(TEXT("at"), At)) WWOShellAnchors::Sample(*Path, Mesh, 1.f, float(At), float(At), Spots);
				else
				{
					const FVector Range = WWOConfig::Vec(O, TEXT("range"), FVector(0.0, 1.0, 0.0));
					WWOShellAnchors::Sample(*Path, Mesh, Spacing, float(Range.X), float(Range.Y), Spots);
				}
				SlideMax = Spacing * 0.5f;
			}
			if (!O->TryGetStringField(TEXT("up"), UpMode)) UpMode = bPath ? TEXT("path") : TEXT("anchor");
			const FVector Off = WWOConfig::Vec(O, TEXT("offset"));
			const float Scale = Num(O, TEXT("scale"), 1.0) * SizeScale;
			const FVector2D ScaleAlong = Pair(O, TEXT("scale_along"), FVector2D(1.0, 1.0));
			const float Zig = Num(O, TEXT("zigzag_cm"), 0.0);
			const float Pitch = Num(O, TEXT("pitch"), 0.0), Roll = Num(O, TEXT("roll"), 0.0);
			const TSharedPtr<FJsonObject> Jit = Object(O, TEXT("jitter"));
			const float JRoll = Jit ? Num(Jit, TEXT("roll"), 0.0) : 0.f, JPitch = Jit ? Num(Jit, TEXT("pitch"), 0.0) : 0.f;
			const float JScale = Jit ? Num(Jit, TEXT("scale"), 0.0) : 0.f, JSlide = Jit ? Num(Jit, TEXT("slide_cm"), 0.0) : 0.f;
			const int32 Seed = int32(Num(O, TEXT("seed"), 1.0));
			const TSharedPtr<FJsonObject> Stack = Object(O, TEXT("stack"));
			const int32 Count = Stack ? FMath::Clamp(int32(Num(Stack, TEXT("count"), 1.0)), 1, 8) : 1;
			const float StepCm = Stack ? Num(Stack, TEXT("step_cm"), 4.0) : 0.f, Shrink = Stack ? Num(Stack, TEXT("shrink"), 0.8) : 1.f;
			// keep_off: volumes no vertex may enter: a capsule between two anchors ({from, to, radius}, the face: from
			// head_front to jaw_front) or a volume the worn parts carry ({volume: name}, or just its name: 'face'). A piece
			// that would is moved up to 'push' cm toward its own -X (its up reversed: the crown's back), else left out.
			// The worn parts' obstacles are kept off too, always: a piece is moved up to 4 cm away from one, else left out.
			struct FKeep { FWWOShellVolume V; float Push; bool bObstacle; };
			TArray<FKeep> Keep;
			TArray<TSharedPtr<FJsonValue>> KeepList;
			const TArray<TSharedPtr<FJsonValue>>* KeepArray = nullptr;
			FString KeepName;
			if (const TSharedPtr<FJsonObject> K = Object(O, TEXT("keep_off"))) KeepList.Add(MakeShared<FJsonValueObject>(K));
			else if (O->TryGetArrayField(TEXT("keep_off"), KeepArray)) KeepList = *KeepArray;
			else if (O->TryGetStringField(TEXT("keep_off"), KeepName)) KeepList.Add(MakeShared<FJsonValueString>(KeepName));
			for (const TSharedPtr<FJsonValue>& V : KeepList)
			{
				const TSharedPtr<FJsonObject> K = V->Type == EJson::Object ? V->AsObject() : nullptr;
				FString Vol, From, To;
				if (V->Type == EJson::String) Vol = V->AsString();
				else if (K.IsValid()) K->TryGetStringField(TEXT("volume"), Vol);
				const float Push = K.IsValid() ? float(Num(K, TEXT("push"), 0.0)) : 0.f;
				if (!Vol.IsEmpty())
				{
					if (const FWWOShellVolume* Found = FindVolume(FName(*Named(Vol)))) Keep.Add({ *Found, Push, false });
					else ++Missing;
					continue;
				}
				if (!K.IsValid() || !K->TryGetStringField(TEXT("from"), From)) continue;
				if (!K->TryGetStringField(TEXT("to"), To)) To = From;
				const FWWOShellPoint* A = FindAnchorPoint(FName(*Named(From)));
				const FWWOShellPoint* B = FindAnchorPoint(FName(*Named(To)));
				if (!A || !B) { ++Missing; continue; }
				FWWOShellVolume Cap;
				Cap.A = A->RefP;
				Cap.B = B->RefP;
				Cap.Radii = FVector(Num(K, TEXT("radius"), 6.0));
				Keep.Add({ Cap, Push, false });
			}
			for (const FWWOShellVolume* V : Obstacles) Keep.Add({ *V, 4.f, true });
			const bool bCap = KitSection == FWWOVineMesh::Fungus && !MeshName.Contains(TEXT("Mycelium")); // its gills light the mycelium round it
			for (int32 i = 0; i < Spots.Num(); ++i)
			{
				// The same draws in the same order every time: the same loadout grows the same way on any robot.
				FRandomStream Rs(Seed * 7919 + i * 131 + (Sign < 0.f ? 104729 : 0));
				const float JR = Rs.FRandRange(-JRoll, JRoll), JP = Rs.FRandRange(-JPitch, JPitch), JS = Rs.FRandRange(-JScale, JScale);
				const float SlideA = Rs.FRandRange(0.f, UE_TWO_PI), SlideD = FMath::Min(Rs.FRand() * JSlide, SlideMax);
				const TArray<FVariant>& Pool = SpotCrack.IsValidIndex(i) && SpotCrack[i] && Cracked.Num() ? Cracked : Plain;
				const FVariant& Var = Pool[Pool.Num() > 1 ? Rs.RandHelper(Pool.Num()) : 0];
				const FWWOLeafShape* Shape = Var.Shape;
				const FWWOShellPoint& S = Spots[i];
				FVector P = S.RefP, N = S.RefN;
				FName Bone = S.Bone;
				// Its +X: the anchor's up, the path's way (or back), or the robot's up laid into the surface.
				const FVector UpWant = UpMode == TEXT("path_reverse") ? -S.RefUp
					: UpMode == TEXT("world") && FMath::Abs(N.Z) < 0.9f ? FVector::UpVector : S.RefUp;
				// Moved over the surface: the offset's up and across, the zigzag (across the path, alternately), the slide.
				const FVector Across0 = FVector::CrossProduct(Tangent(N, UpWant), N);
				const FVector PathAcross = FVector::CrossProduct(S.RefUp, N).GetSafeNormal();
				const float ZigHere = (i % 2 ? -Zig : Zig) * Sign;
				if (Off.Z != 0.0 || Off.Y != 0.0 || ZigHere != 0.f || SlideD > 0.05f)
				{
					const FWWOShellSurface& Sf = SurfaceNow();
					FName Walked = Bone;
					if (Off.Z != 0.0) Sf.Walk(P, N, Tangent(N, UpWant), float(Off.Z), &Walked);
					if (Off.Y != 0.0) Sf.Walk(P, N, Across0, float(Off.Y) * Sign, &Walked);
					if (ZigHere != 0.f) Sf.Walk(P, N, PathAcross, ZigHere, &Walked);
					if (SlideD > 0.05f) Sf.Walk(P, N, Tangent(N, UpWant).RotateAngleAxisRad(SlideA, N), SlideD, &Walked);
					if (FVector::Dist(P, S.RefP) > 0.5f) Bone = Walked;
				}
				const float F = Spots.Num() > 1 ? float(i) / (Spots.Num() - 1) : 0.f;
				const float Sc = Scale * FMath::Lerp(float(ScaleAlong.X), float(ScaleAlong.Y), F) * (1.f + JS);
				for (int32 c = 0; c < Count; ++c)
				{
					FVector Pc = P, Nc = N;
					FName Bc = Bone;
					if (c > 0) SurfaceNow().Walk(Pc, Nc, Tangent(Nc, UpWant), StepCm * c, &Bc);
					const float ScC = Sc * FMath::Pow(Shrink, float(c));
					// Built in its frame (X up, Z out), pitched about its Y (+: Z toward X, a shelf's rim up), rolled about Z.
					auto Place = [&](const FVector& At, const FVector& Nn)
					{
						const FQuat Q = FRotationMatrix::MakeFromXZ(Tangent(Nn, UpWant), Nn).ToQuat()
							* FQuat(FVector::ZAxisVector, FMath::DegreesToRadians(Roll + JR)) * FQuat(FVector::YAxisVector, FMath::DegreesToRadians(Pitch + JP));
						return FTransform(Q, At + Nn * Off.X, FVector(ScC));
					};
					FTransform Xf = Place(Pc, Nc);
					auto Clashes = [&](const FTransform& X) -> const FKeep*
					{
						for (const FKeep& K : Keep)
						{
							for (int32 v = 0; v < Shape->V.Num(); v += 3) if (K.V.Contains(X.TransformPosition(Shape->V[v]))) return &K;
							// A thin obstacle (an antenna) can pass through a big triangle between its corners: its axis is tested too.
							if (!K.bObstacle || K.V.Kind != FWWOShellVolume::Capsule) continue;
							const FTransform Inv = X.Inverse();
							const FVector A = Inv.TransformPosition(K.V.A), B = Inv.TransformPosition(K.V.B);
							if (!Shape->Bounds.ExpandBy(K.V.Radii.X / FMath::Max(float(X.GetScale3D().X), 0.01f)).Intersect(FBox(TArray<FVector>{ A, B }))) continue;
							const TArray<int32>& T = Shape->Tris[0];
							for (int32 t = 0; t + 2 < T.Num(); t += 3)
							{
								FVector Hit, HitN;
								if (FMath::SegmentTriangleIntersection(A, B, Shape->V[T[t]], Shape->V[T[t + 1]], Shape->V[T[t + 2]], Hit, HitN)) return &K;
							}
						}
						return nullptr;
					};
					bool bKeep = true;
					const FKeep* Hit = Clashes(Xf);
					if (Hit)
					{
						bKeep = false;
						// Away from an obstacle (over the surface, from where the piece's middle is), else back along its own -X.
						const FVector Mid = Xf.TransformPosition(Shape->Bounds.GetCenter());
						const FVector Way = Hit->bObstacle ? Tangent(Nc, Hit->V.Away(Mid)) : -Tangent(Nc, UpWant);
						for (float Back = 1.f; Back <= Hit->Push; Back += 1.f)
						{
							FVector Pb = Pc, Nb = Nc;
							SurfaceNow().Walk(Pb, Nb, Way, Back, &Bc);
							Xf = Place(Pb, Nb);
							if (!Clashes(Xf)) { bKeep = true; break; }
						}
					}
					if (Hit && Hit->bObstacle) ++(bKeep ? ObstaclePushed : ObstacleLeftOut);
					if (!bKeep) { KeptOff += Hit->bObstacle ? 0 : 1; continue; }
					VinesNow().KitPiece(Bc, Var.Shapes, Var.SmallShapes, Xf, KitSection);
					if (bCap) Glow.Add(FVector4(Xf.TransformPosition(Shape->Bounds.GetCenter()), FMath::Max(Shape->Bounds.GetExtent().GetMax() * ScC, 4.0)));
				}
			}
		};
		// VeilSpore's mycelium: along a chain of paths ('paths', as a vine's), or round an anchor; grown last.
		auto PlaceNet = [&](const TSharedPtr<FJsonObject>& O, const TFunctionRef<FString(const FString&)> Named, float Sign)
		{
			const FWWONetStyle St = NetStyle(O);
			const int32 Seed = int32(Num(O, TEXT("seed"), 1.0)) * 7919 + (Sign < 0.f ? 104729 : 0);
			FString AnchorName;
			if (O->TryGetStringField(TEXT("anchor"), AnchorName))
			{
				const FWWOShellPoint* P = FindAnchorPoint(FName(*Named(AnchorName)));
				if (!P) { ++Missing; return; }
				Later.Add([&, P, St, Seed]() { VinesNow().NetPatch(P->Bone, P->RefP, P->RefN, St, SurfaceNow(), Glow, Seed); });
				return;
			}
			FWWOVineLine Line;
			if (!VineLine(O, Named, Sign, Line)) { ++Missing; return; }
			Later.Add([&, Line, St, Seed, Sign]() { VinesNow().Net(Line, St, SurfaceNow(), Sign, Glow, Seed); });
		};
		auto Section = [&](const FString& Name)
		{
			const TArray<TSharedPtr<FJsonValue>>* List = nullptr;
			if (!R->TryGetArrayField(Name, List)) return;
			for (const TSharedPtr<FJsonValue>& V : *List)
			{
				const TSharedPtr<FJsonObject> O = V->AsObject();
				if (!O.IsValid()) continue;
				FString AnchorName, PathName, Piece;
				O->TryGetStringField(TEXT("anchor"), AnchorName);
				O->TryGetStringField(TEXT("path"), PathName);
				O->TryGetStringField(TEXT("piece"), Piece);
				// "{s}": once for each side, l then r.
				TArray<TPair<FString, float>> Sides;
				if (IsSided(O)) Sides = { { TEXT("l"), 1.f }, { TEXT("r"), -1.f } };
				else Sides = { { TEXT(""), 1.f } };
				for (const TPair<FString, float>& Side : Sides)
				{
					auto Named = [&Side](const FString& T) { return T.Replace(TEXT("{s}"), *Side.Key); };
					if (IsVinePiece(Piece))
					{
						PlaceVine(O, Piece, Named, Side.Value);
					}
					else if (Piece == TEXT("mesh"))
					{
						PlaceMesh(O, Named, Side.Value);
					}
					else if (Piece == TEXT("mycelium"))
					{
						PlaceNet(O, Named, Side.Value);
					}
					else if (!AnchorName.IsEmpty())
					{
						const FWWOShellPoint* P = FindAnchorPoint(FName(*Named(AnchorName)));
						if (!P) { ++Missing; continue; }
						// scale_by_size: as big as the spot it sits on allows (size_ref cm is the recipe's own size).
						const float Scale = Flag(O, TEXT("scale_by_size"), false) ? FMath::Clamp(P->Size / float(Num(O, TEXT("size_ref"), 5.0)), 0.6f, 1.6f) : 1.f;
						Place(*P, Scale, Side.Value, O);
					}
					else
					{
						const FWWOShellPath* Path = FindPath(FName(*Named(PathName)));
						if (!Path) { ++Missing; continue; }
						const FVector Range = WWOConfig::Vec(O, TEXT("range"), FVector(0.0, 1.0, 0.0));
						TArray<FWWOShellPoint> Samples;
						WWOShellAnchors::Sample(*Path, Mesh, float(Num(O, TEXT("spacing_cm"), 8.0)), float(Range.X), float(Range.Y), Samples);
						for (const FWWOShellPoint& P : Samples) Place(P, 1.f, Side.Value, O);
					}
				}
			}
		};
		if (!In.Grenade.IsNone())
		{
			Section(TEXT("grenade"));
			Section(TEXT("grenade.") + In.Grenade.ToString()); // this grenade's own pieces
		}
		for (const FName& Id : In.Ids) Section(Id.ToString());
		for (const TFunction<void()>& Grow : Later) Grow();
	
		// Every bone's mesh at every level: the vines' and kit's, with the plates and pods added.
		TMap<FName, FWWOVineMeshLevels> Rigid;
		if (Vines) Rigid = MoveTemp(Vines->Rigid);
		for (int32 L = 0; L < NumGraftLevels; ++L)
		{
			for (const TPair<FName, TUniquePtr<FWWOGraftBuilder>>& It : Builders[L])
			{
				if (It.Value && !It.Value->IsEmpty()) It.Value->BakeInto(Rigid.FindOrAdd(It.Key).L[L]);
			}
		}
		int32 SecTris[NumGraftLevels][FWWOVineMesh::NumSections] = {};
		for (TPair<FName, FWWOVineMeshLevels>& It : Rigid)
		{
			if (It.Value.IsEmpty()) continue;
			// Where an empty level's stand-in triangle goes: anywhere on the piece.
			FVector Fallback = FVector::ZeroVector;
			for (const FWWOVineMesh::FSection& Sec : It.Value.L[0].S) if (Sec.V.Num()) { Fallback = Sec.V[0]; break; }
			const int32 Parts = FMath::Max(1, FMath::DivideAndRoundUp(It.Value.L[0].NumTris(), MaxPartTris));
			for (int32 Part = 0; Part < Parts; ++Part)
			{
				Out->Bones.Add(It.Key);
				TArray<FMeshDescription>& Levels = Out->Levels.AddDefaulted_GetRef();
				Levels.SetNum(NumGraftLevels);
				FIntVector Tris;
				FWWOVineMesh Cuts[NumGraftLevels];
				TArray<int32>& Used = Out->Sections.AddDefaulted_GetRef();
				for (int32 L = 0; L < NumGraftLevels; ++L)
				{
					if (Parts > 1) for (int32 s = 0; s < FWWOVineMesh::NumSections; ++s) CutSection(It.Value.L[L].S[s], Part, Parts, Cuts[L].S[s]);
				}
				for (int32 s = 0; s < FWWOVineMesh::NumSections; ++s)
				{
					for (int32 L = 0; L < NumGraftLevels; ++L)
					{
						if ((Parts > 1 ? Cuts[L] : It.Value.L[L]).S[s].Tris.Num() > 0) { Used.Add(s); break; }
					}
				}
				for (int32 L = 0; L < NumGraftLevels; ++L)
				{
					const FWWOVineMesh& M = Parts > 1 ? Cuts[L] : It.Value.L[L];
					Describe(M, Fallback, Used, Levels[L]);
					Tris[L] = M.NumTris();
					Out->Tris[L] += Tris[L];
					for (int32 s = 0; s < FWWOVineMesh::NumSections; ++s) SecTris[L][s] += M.S[s].Tris.Num() / 3;
				}
				Out->BoneTris.Add(Tris);
			}
		}
		int32 JointTris[NumGraftLevels] = { 0, 0, 0 };
		if (Vines && !Vines->Joints->IsEmpty())
		{
			Out->Joints = Vines->Joints;
			for (int32 L = 0; L < NumGraftLevels; ++L)
			{
				JointTris[L] = Out->Joints->NumTris(L);
				Out->Tris[L] += JointTris[L];
				for (int32 s = 0; s < FWWOVineMesh::NumSections; ++s) SecTris[L][s] += Out->Joints->S[L][s].Tris.Num() / 3;
			}
		}
		Out->WorkMs = (FPlatformTime::Seconds() - Started) * 1000.0;
		// Per level: all its triangles, by material (bark, core, leaf, crystal, stem, fungus, plates, pods, stalks).
		static const TCHAR* SectionNames[FWWOVineMesh::NumSections] = { TEXT("bark"), TEXT("core"), TEXT("leaf"), TEXT("crystal"), TEXT("stem"),
			TEXT("fungus"), TEXT("scales"), TEXT("plates"), TEXT("pods"), TEXT("stalks") };
		FString Levels;
		for (int32 L = 0; L < NumGraftLevels; ++L)
		{
			FString By;
			for (int32 s = 0; s < FWWOVineMesh::NumSections; ++s) if (SecTris[L][s]) By += FString::Printf(TEXT("%s%s %d"), By.IsEmpty() ? TEXT("") : TEXT(", "),
				SectionNames[s], SecTris[L][s]);
			if (Vines && SecTris[L][FWWOVineMesh::Fungus]) By += FString::Printf(TEXT(": kit %d, mycelium %d"), Vines->KitTris[L], Vines->NetTris[L]);
			Levels += FString::Printf(TEXT(" L%d %d (%s; joints %d)"), L, Out->Tris[L], *By, JointTris[L]);
		}
		Out->Summary = FString::Printf(TEXT("anchor grafts for %s (%s) on %s, %s, %s, %s: %d pieces in %d meshes, %d anchors or paths missing, %d kept off the face, leaves %d (kept %d/%d/%d), seated %d (mean gap %.2f -> %.2f cm), obstacles %d: %d pieces pushed, %d left out, %d net knots left out, %d vine points inside; triangles%s; built in %.2f ms off the game thread"),
			*In.Flora.ToString(), *In.Seed, *In.Parts[0].ToString(), *In.Parts[1].ToString(), *In.Parts[2].ToString(), *In.Parts[3].ToString(),
			Pieces + (Vines ? Vines->Pieces : 0), Out->Bones.Num() + (Out->Joints ? 1 : 0), Missing, KeptOff, Vines ? Vines->Leaves : 0,
			Vines ? Vines->LeavesKept[0] : 0, Vines ? Vines->LeavesKept[1] : 0, Vines ? Vines->LeavesKept[2] : 0,
			Seated, Seated ? SeatGapBefore / Seated : 0.f, Seated ? SeatGapAfter / Seated : 0.f,
			Obstacles.Num(), ObstaclePushed, ObstacleLeftOut, Vines ? Vines->KnotsBlocked : 0, VineInObstacles, *Levels, Out->WorkMs);
		return Out;
	}
}

// --- The built grafts, kept and shared (game thread) ---------------------------------------------

// One kit on one set of worn parts, made: a static mesh per bone (its LODs the detail levels) and the joint sections.
struct FWWOAnchorGraftResult
{
	struct FBoneMesh { FName Bone; TObjectPtr<UStaticMesh> Mesh; };
	TArray<FBoneMesh> Meshes;
	TSharedPtr<const FWWOVineJoints> Joints;
	int32 Tris[NumGraftLevels] = { 0, 0, 0 };
};

class FWWOAnchorGraftCache : public FGCObject
{
public:
	static FWWOAnchorGraftCache& Get()
	{
		static FWWOAnchorGraftCache* Cache = new FWWOAnchorGraftCache(); // (never freed: it outlives every world)
		return *Cache;
	}

	// A body wants this build: given it when it is ready (on a later frame, within the frame budget), if the body still
	// wants that request then.
	void Request(FAnchorInputs&& In, UWWOBodyComponent* Body, int32 RequestId)
	{
		TSharedPtr<FEntry>& E = Entries.FindOrAdd(In.Key);
		const bool bNew = !E.IsValid();
		if (bNew)
		{
			E = MakeShared<FEntry>();
			E->Key = In.Key;
			E->Asked = FPlatformTime::Seconds();
			E->Asker = In.Asker;
			E->Inputs = MakeShared<FAnchorInputs>(MoveTemp(In));
			// What it copies and draws with, loaded in the background if it isn't yet (Tick starts the build once it is).
			TArray<FString> Paths = E->Inputs->KitPaths;
			Paths.Append(FWWOVineGraft::LeafPaths());
			for (int32 s = 0; s < FWWOVineMesh::NumSections; ++s) Paths.Add(FWWOVineMesh::MaterialPath(FWWOVineMesh::ESection(s)));
			const TWeakPtr<FEntry> Weak = E;
			for (const FString& Path : Paths)
			{
				if (FindObject<UObject>(nullptr, *Path)) continue;
				++E->Loading;
				LoadPackageAsync(FPackageName::ObjectPathToPackageName(Path), FLoadPackageAsyncDelegate::CreateLambda(
					[Weak](const FName&, UPackage*, EAsyncLoadingResult::Type) { if (const TSharedPtr<FEntry> Entry = Weak.Pin()) --Entry->Loading; }));
			}
		}
		E->Used = FPlatformTime::Seconds();
		Waiting.RemoveAll([Body](const FWaiter& W) { return W.Body.Get() == Body; });
		Waiting.Add({ Body, RequestId, E, bNew ? FString(TEXT("built")) : E->Result ? FString(TEXT("cached")) : FString(TEXT("joined a build")) });
		if (!TickHandle.IsValid())
		{
			TickHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateRaw(this, &FWWOAnchorGraftCache::Tick));
		}
	}

	virtual void AddReferencedObjects(FReferenceCollector& Collector) override
	{
		for (TPair<FString, TSharedPtr<FEntry>>& It : Entries)
		{
			if (!It.Value->Result) continue;
			for (FWWOAnchorGraftResult::FBoneMesh& M : It.Value->Result->Meshes) Collector.AddReferencedObject(M.Mesh);
		}
		for (TObjectPtr<UMaterialInterface>& M : Materials) Collector.AddReferencedObject(M);
	}
	virtual FString GetReferencerName() const override { return TEXT("FWWOAnchorGraftCache"); }

	// The meshes' materials (loaded once a run, when the first build is asked for).
	void LoadMaterials()
	{
		if (Materials.Num() > 0) return;
		const double Started = FPlatformTime::Seconds();
		for (int32 s = 0; s < FWWOVineMesh::NumSections; ++s) Materials.Add(FWWOVineMesh::Material(FWWOVineMesh::ESection(s)));
		UE_LOG(LogWWO, Log, TEXT("Anchor grafts: their materials loaded in %.2f ms"), (FPlatformTime::Seconds() - Started) * 1000.0);
	}

private:
	struct FJob { TSharedPtr<FAnchorBuilt> Built; }; // the worker's result (read once the task is done)
	struct FEntry
	{
		FString Key, Asker;
		TSharedPtr<FAnchorInputs> Inputs; // until the build starts
		int32 Loading = 0;                // packages still loading for it
		TArray<TStrongObjectPtr<UObject>> Keep; // what the build copies, kept loaded while it runs
		bool bResolved = false, bLaunched = false;
		UE::Tasks::FTask Task;
		TSharedPtr<FJob> Job;
		TSharedPtr<FAnchorBuilt> Built;              // from the worker
		TSharedPtr<FWWOAnchorGraftResult> Result;    // made from it, bone by bone; ready when Next reaches the end
		int32 Next = 0;
		bool bReady = false;
		double Asked = 0.0, Used = 0.0;
		int32 Frames = 0;
		double LongestMs = 0.0, MeshMs = 0.0;
	};
	struct FWaiter
	{
		TWeakObjectPtr<UWWOBodyComponent> Body;
		int32 Request = 0;
		TSharedPtr<FEntry> Entry;
		FString How;
	};
	TMap<FString, TSharedPtr<FEntry>> Entries;
	TArray<FWaiter> Waiting;
	TArray<TObjectPtr<UMaterialInterface>> Materials; // by section, loaded once
	FTSTicker::FDelegateHandle TickHandle;
	static constexpr int32 MaxEntries = 16;

	UStaticMesh* MakeMesh(const TArray<FMeshDescription>& Levels, const TArray<int32>& Sections, FName Bone)
	{
		LoadMaterials();
		UPackage* Outer = GetTransientPackage();
		UStaticMesh* SM = NewObject<UStaticMesh>(Outer, MakeUniqueObjectName(Outer, UStaticMesh::StaticClass(), FName(*(TEXT("SM_AnchorGraft_") + Bone.ToString()))), RF_Transient);
		SM->bSupportRayTracing = false;
		TArray<FStaticMaterial> Slots;
		for (const int32 s : Sections)
		{
			FStaticMaterial& Slot = Slots.Add_GetRef(FStaticMaterial(Materials[s], SlotName(s)));
			Slot.UVChannelData = FMeshUVChannelInfo(1.f); // (a cooked game never works this out for a mesh made at run time, and asks for it)
		}
		SM->SetStaticMaterials(Slots);
		UStaticMesh::FBuildMeshDescriptionsParams Params;
		Params.bMarkPackageDirty = false;
		Params.bCommitMeshDescription = false;
		Params.bFastBuild = true;
		Params.bBuildSimpleCollision = false;
		TArray<const FMeshDescription*> Ptrs;
		for (const FMeshDescription& MD : Levels) Ptrs.Add(&MD);
		SM->BuildFromMeshDescriptions(Ptrs, Params);
		// No collision: the build gives every mesh a body setup that would cook its triangles (complex collision) when asked,
		// and their CPU copy isn't kept, which warned once per mesh. Simple-as-complex with no simple shapes: nothing to cook.
		if (UBodySetup* Body = SM->GetBodySetup()) Body->CollisionTraceFlag = CTF_UseSimpleAsComplex;
		return SM;
	}

	bool Tick(float)
	{
		const double Start = FPlatformTime::Seconds();
		const double Budget = FMath::Max(BudgetMs, 0.1f) / 1000.0;
		bool bDid = false;
		auto Spare = [&]() { return !bDid || FPlatformTime::Seconds() - Start < Budget; };
		// Builds whose meshes have loaded: started on a worker thread (the leaves' and kit's meshes kept loaded for it).
		for (TPair<FString, TSharedPtr<FEntry>>& It : Entries)
		{
			FEntry& E = *It.Value;
			if (E.bLaunched || E.Loading > 0) continue;
			FAnchorInputs& In = *E.Inputs;
			if (!E.bResolved)
			{
				E.bResolved = true;
				auto Resolve = [&](const FString& Path) -> const UStaticMesh*
				{
					UStaticMesh* SM = FindObject<UStaticMesh>(nullptr, *Path);
					if (!SM) SM = LoadObject<UStaticMesh>(nullptr, *Path, nullptr, LOAD_NoWarn | LOAD_Quiet); // (not loaded in the background: a missing asset, logged by the build)
					if (SM) E.Keep.Emplace(SM);
					return SM;
				};
				for (const FString& Path : In.KitPaths) In.KitMeshes.Add(Resolve(Path));
				for (const FString& Path : FWWOVineGraft::LeafPaths()) In.LeafMeshes.Add(Resolve(Path));
				LoadMaterials();
				bDid = true;
			}
			// (In the editor a just-loaded mesh may still be building its render data, which only the game thread may wait
			// for: the build starts once none is.)
			if (E.Keep.ContainsByPredicate([](const TStrongObjectPtr<UObject>& O) { const UStaticMesh* SM = Cast<UStaticMesh>(O.Get()); return SM && SM->IsCompiling(); })) continue;
			TSharedPtr<FAnchorInputs> Inputs = MoveTemp(E.Inputs);
			TSharedPtr<FJob> Job = MakeShared<FJob>();
			E.Job = Job;
			E.Task = UE::Tasks::Launch(UE_SOURCE_LOCATION, [Job, Inputs]() { Job->Built = BuildGeometry(*Inputs); });
			E.bLaunched = true;
			bDid = true;
			if (!Spare()) break;
		}
		// Builds whose geometry is done and that a robot is waiting for: their meshes, a bone at a time. (A loadout
		// changed piece by piece asks for a few kits on the way; those nobody wants by then are never made.)
		for (TPair<FString, TSharedPtr<FEntry>>& It : Entries)
		{
			FEntry& E = *It.Value;
			if (E.bReady || !E.bLaunched || !E.Task.IsCompleted()) continue;
			E.Keep.Reset(); // (the shapes are copied)
			if (!Waiting.ContainsByPredicate([&](const FWaiter& W) { return W.Entry == It.Value && W.Body.IsValid(); })) continue;
			if (!E.Result)
			{
				E.Built = E.Job->Built ? E.Job->Built : MakeShared<FAnchorBuilt>();
				E.Job.Reset();
				E.Result = MakeShared<FWWOAnchorGraftResult>();
				E.Result->Joints = E.Built->Joints;
				for (int32 L = 0; L < NumGraftLevels; ++L) E.Result->Tris[L] = E.Built->Tris[L];
				UE_LOG(LogWWO, Log, TEXT("Body %s: %s"), *E.Asker, *E.Built->Summary);
			}
			const double EntryStart = FPlatformTime::Seconds();
			while (E.Next < E.Built->Bones.Num() && Spare())
			{
				const double MeshStart = FPlatformTime::Seconds();
				E.Result->Meshes.Add({ E.Built->Bones[E.Next], MakeMesh(E.Built->Levels[E.Next], E.Built->Sections[E.Next], E.Built->Bones[E.Next]) });
				E.MeshMs = FMath::Max(E.MeshMs, (FPlatformTime::Seconds() - MeshStart) * 1000.0);
				++E.Next;
				bDid = true;
			}
			++E.Frames;
			E.LongestMs = FMath::Max(E.LongestMs, (FPlatformTime::Seconds() - EntryStart) * 1000.0);
			if (E.Next >= E.Built->Bones.Num())
			{
				E.bReady = true;
				UE_LOG(LogWWO, Log, TEXT("Anchor grafts ready (%s): %d meshes made over %d frames, the longest frame %.2f ms (the biggest mesh %.2f ms), %.0f ms after it was asked for"),
					*E.Key, E.Result->Meshes.Num(), E.Frames, E.LongestMs, E.MeshMs, (FPlatformTime::Seconds() - E.Asked) * 1000.0);
				E.Built.Reset(); // (the mesh descriptions: not needed again)
			}
			if (!Spare()) break;
		}
		// Bodies waiting for a ready build: theirs, as many as fit.
		for (int32 i = 0; i < Waiting.Num() && Spare(); )
		{
			FWaiter& W = Waiting[i];
			if (!W.Body.IsValid()) { Waiting.RemoveAt(i); continue; }
			if (!W.Entry->bReady) { ++i; continue; }
			const FWaiter Done = W;
			Waiting.RemoveAt(i);
			Done.Body->ApplyAnchorGrafts(*Done.Entry->Result, Done.Request, Done.How);
			bDid = true;
		}
		// Not too many kept: the least lately used of those nobody waits for go (a robot wearing them keeps its meshes).
		while (Entries.Num() > MaxEntries)
		{
			FString Oldest;
			double OldestUsed = TNumericLimits<double>::Max();
			for (const TPair<FString, TSharedPtr<FEntry>>& It : Entries)
			{
				if (!It.Value->bLaunched || !It.Value->Task.IsCompleted() || It.Value->Used >= OldestUsed) continue;
				if (Waiting.ContainsByPredicate([&](const FWaiter& W) { return W.Entry == It.Value; })) continue;
				Oldest = It.Key;
				OldestUsed = It.Value->Used;
			}
			if (Oldest.IsEmpty()) break;
			Entries.Remove(Oldest);
		}
		const double Ms = (FPlatformTime::Seconds() - Start) * 1000.0;
		if (bDid && Ms > LongestFrameMs)
		{
			LongestFrameMs = Ms;
			UE_LOG(LogWWO, Log, TEXT("Anchor grafts: the longest frame of making meshes and handing them out so far: %.2f ms"), Ms);
		}
		bool bBusy = Waiting.Num() > 0;
		for (const TPair<FString, TSharedPtr<FEntry>>& It : Entries) bBusy |= !It.Value->bLaunched || !It.Value->Task.IsCompleted();
		if (!bBusy) TickHandle.Reset();
		return bBusy;
	}
	double LongestFrameMs = 0.0;
};

// --- Anchor grafts on a body ----------------------------------------------------------------------

void UWWOBodyComponent::RebuildAllGrafts(UWorld* World)
{
	for (TObjectIterator<UWWOBodyComponent> It; It; ++It)
	{
		UWorld* W = It->GetWorld();
		if (It->IsTemplate() || !W || !W->IsGameWorld() || (World && W != World)) continue;
		if (It->bFollowsLoadout && It->HasShells()) It->RebuildGrafts();
	}
}

bool UWWOBodyComponent::UseAnchorGrafts(FName FloraId) const
{
	const int32 Mode = AnchorGraftsNow();
	const bool bOn = Mode >= 0 ? Mode > 0 : bModelAnchorGrafts;
	return bOn && HasShells() && Recipe(FloraId).IsValid();
}

void UWWOBodyComponent::DestroyAnchorParts()
{
	if (VineJoints) VineJoints->Stop();
	VineJoints.Reset();
	for (const TWeakObjectPtr<UPrimitiveComponent>& Part : AnchorParts)
	{
		if (UPrimitiveComponent* C = Part.Get())
		{
			Grafts.Remove(C);
			C->DestroyComponent();
		}
	}
	AnchorParts.Reset();
	if (USkeletalMeshComponent* M = AnchorLodModel.Get(); M && AnchorLodHandle.IsValid()) M->UnregisterOnBoneTransformsFinalizedDelegate(AnchorLodHandle);
	AnchorLodHandle.Reset();
	AnchorLodModel = nullptr;
}

void UWWOBodyComponent::ClearAnchorGrafts()
{
	++AnchorRequest; // (a build still on its way is no longer wanted)
	DestroyAnchorParts();
}

void UWWOBodyComponent::BuildAnchorGrafts()
{
	// Asks for this kit on the parts worn now; what is shown stays until it is ready (ApplyAnchorGrafts).
	const double Started = FPlatformTime::Seconds();
	const TSharedPtr<FJsonObject> R = Recipe(AnchorFlora);
	const USkeletalMesh* Mesh = HasShells() ? Model->GetSkeletalMeshAsset() : nullptr;
	if (!R.IsValid() || !Mesh)
	{
		ClearAnchorGrafts();
		return;
	}
	FAnchorInputs In;
	In.Flora = AnchorFlora;
	In.Grenade = AnchorGrenade;
	In.Ids = AnchorIds;
	In.Mesh = Mesh;
	In.Recipe = R;
	In.bJoints = VineJointsMode != 0;
	ReadDetail(R, In.Detail);
	In.Seed = AnchorGrenade.ToString();
	for (const FName& Id : AnchorIds) In.Seed += TEXT("|") + Id.ToString();
	In.Key = AnchorFlora.ToString() + TEXT("|") + In.Seed + (In.bJoints ? TEXT("|joints|") : TEXT("|rigid|")) + Mesh->GetName();
	for (int32 Slot = 0; Slot < NumShellSlots; ++Slot)
	{
		In.Parts[Slot] = WornParts[Slot];
		In.Key += TEXT("|") + WornParts[Slot].ToString();
	}
	In.Parts[NumShellSlots] = SeamPart;
	In.Asker = GetOwner()->GetName();
	// The kit's pieces it copies (VeilSpore's).
	FString Kit;
	if (R->TryGetStringField(TEXT("kit"), Kit))
	{
		for (const TPair<FString, TSharedPtr<FJsonValue>>& Section : R->Values)
		{
			const TArray<TSharedPtr<FJsonValue>>* List = nullptr;
			if (!Section.Value.IsValid() || !Section.Value->TryGetArray(List)) continue;
			for (const TSharedPtr<FJsonValue>& V : *List)
			{
				FString MeshName;
				const TSharedPtr<FJsonObject> O = V.IsValid() ? V->AsObject() : nullptr;
				if (!O.IsValid()) continue;
				auto Want = [&](const FString& Name) { In.KitPaths.AddUnique(FString::Printf(TEXT("%s/%s.%s"), *Kit, *Name, *Name)); };
				auto WantList = [&](const TSharedPtr<FJsonObject>& From, const TCHAR* Field)
				{
					const TArray<TSharedPtr<FJsonValue>>* Names = nullptr;
					if (From.IsValid() && From->TryGetArrayField(Field, Names)) for (const TSharedPtr<FJsonValue>& N : *Names) Want(N->AsString());
				};
				if (O->TryGetStringField(TEXT("mesh"), MeshName)) Want(MeshName);
				WantList(O, TEXT("meshes"));
				const TSharedPtr<FJsonObject>* GridObject = nullptr;
				if (O->TryGetObjectField(TEXT("grid"), GridObject)) WantList(*GridObject, TEXT("crack"));
			}
		}
	}
	FWWOAnchorGraftCache::Get().Request(MoveTemp(In), this, ++AnchorRequest);
	AnchorAskedMs = FMath::Max(AnchorAskedMs, (FPlatformTime::Seconds() - Started) * 1000.0); // (the dearest ask since the last given)
}

int32 UWWOBodyComponent::AnchorLevelNow() const
{
	if (ForcedLevel >= 0) return FMath::Min(ForcedLevel, NumGraftLevels - 1);
	if (bFullDetailGrafts || !Model) return 0;
	// The robot's own LOD (its skeleton's, which the shell parts follow): the one UpdateRobotLod picked from its screen size
	// against SK_RobotSkeleton's LOD screen sizes, with hysteresis, and forced on it.
	return FMath::Clamp(Model->GetPredictedLODLevel(), 0, NumGraftLevels - 1);
}

void UWWOBodyComponent::UpdateAnchorLevel()
{
	const int32 Level = AnchorLevelNow();
	if (Level == AnchorLevel) return;
	if (++AnchorLevelSwitches <= 12)
	{
		// (How far the camera is, and its field of view: to check where the switches fall.)
		const APlayerController* PC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;
		const APlayerCameraManager* Cam = PC ? PC->PlayerCameraManager.Get() : nullptr;
		UE_LOG(LogWWO, Log, TEXT("Body %s: anchor grafts level %d -> %d (robot LOD %d) at %.1f m from the camera (field of view %.0f)"), *GetOwner()->GetName(), AnchorLevel, Level,
			Model ? Model->GetPredictedLODLevel() : -1, Cam ? FVector::Dist(Cam->GetCameraLocation(), Model ? Model->Bounds.Origin : GetOwner()->GetActorLocation()) / WWO::M : -1.f, Cam ? Cam->GetFOVAngle() : 0.f);
	}
	AnchorLevel = Level;
	for (const TWeakObjectPtr<UPrimitiveComponent>& Part : AnchorParts)
	{
		if (UStaticMeshComponent* S = Cast<UStaticMeshComponent>(Part.Get())) S->SetForcedLodModel(Level + 1);
	}
	if (VineJoints) VineJoints->SetLevel(Level);
}

void UWWOBodyComponent::ApplyAnchorGrafts(const FWWOAnchorGraftResult& Result, int32 Request, const FString& How)
{
	if (Request != AnchorRequest || !HasShells() || !GetOwner()) return; // asked again since, or the model went
	const double Started = FPlatformTime::Seconds();
	DestroyAnchorParts();
	const USkeletalMesh* Mesh = Model->GetSkeletalMeshAsset();
	AnchorLevel = AnchorLevelNow();
	// Each piece is hung on its bone: its vertices are in the mesh's space, so the bone's reference pose is
	// undone (in that pose it sits exactly where it was built). NAME_None: on the model itself (the vines' joints).
	auto Hang = [&](UPrimitiveComponent* C, FName Bone)
	{
		C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		C->SetGenerateOverlapEvents(false);
		C->SetCanEverAffectNavigation(false);
		C->LightingChannels = Model->LightingChannels; // lit like the body (the menu preview has its own lights)
		C->SetupAttachment(Model, Bone);
		C->SetRelativeTransform(Bone.IsNone() ? FTransform::Identity : WWOShellAnchors::RefBone(Mesh, Bone).Inverse());
		C->RegisterComponent();
		GetOwner()->AddInstanceComponent(C);
		ApplyShadowOnly(C); // your own: hidden from you, casting your shadow, as the model is
		C->bCastHiddenShadow = bShadowOnly;
		Grafts.Add(C);
		AnchorParts.Add(C);
	};
	for (const FWWOAnchorGraftResult::FBoneMesh& M : Result.Meshes)
	{
		if (!M.Mesh) continue;
		UStaticMeshComponent* C = NewObject<UStaticMeshComponent>(GetOwner());
		C->SetMobility(EComponentMobility::Movable);
		C->SetStaticMesh(M.Mesh);
		C->ForcedLodModel = AnchorLevel + 1;
		Hang(C, M.Bone);
	}
	if (Result.Joints && !Result.Joints->IsEmpty())
	{
		UProceduralMeshComponent* C = NewObject<UProceduralMeshComponent>(GetOwner());
		C->bUseAsyncCooking = false;
		VineJoints = MakeShared<FWWOVineJoints>(*Result.Joints);
		Hang(C, NAME_None);
		VineJoints->Start(C, Model, AnchorLevel);
	}
	static bool bToldLods = false;
	if (!bToldLods)
	{
		// (The robot's LODs the levels follow: at what screen size each starts.)
		bToldLods = true;
		FString Sizes;
		for (int32 i = 0; i < Mesh->GetLODNum(); ++i) if (const FSkeletalMeshLODInfo* Info = Mesh->GetLODInfo(i)) Sizes += FString::Printf(TEXT(" %.3f"), Info->ScreenSize.Default);
		const IConsoleVariable* Bias = IConsoleManager::Get().FindConsoleVariable(TEXT("r.SkeletalMeshLODBias"));
		// The engine's screen size is the bounding sphere's diameter over the distance, times max(1, aspect) / tan(fov / 2) / 2
		// (the projection's larger scale): each LOD's distance on a 16:9 screen at 90 degrees and at the camera's field of view.
		const APlayerController* PC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;
		const float CamFov = PC && PC->PlayerCameraManager ? PC->PlayerCameraManager->GetFOVAngle() : 90.f;
		const float Radius = Model->Bounds.SphereRadius;
		FString Dists;
		for (const float Fov : { 90.f, CamFov })
		{
			const float Multiple = 0.5f * (16.f / 9.f) / FMath::Tan(FMath::DegreesToRadians(FMath::Clamp(Fov, 5.f, 170.f) * 0.5f));
			Dists += FString::Printf(TEXT("%sat %.0f degrees"), Dists.IsEmpty() ? TEXT("") : TEXT("; "), Fov);
			for (int32 i = 1; i < Mesh->GetLODNum(); ++i) if (const FSkeletalMeshLODInfo* Info = Mesh->GetLODInfo(i); Info && Info->ScreenSize.Default > 0.f)
				Dists += FString::Printf(TEXT(" LOD %d from %.1f m"), i, 2.f * Multiple * Radius / Info->ScreenSize.Default / WWO::M);
		}
		UE_LOG(LogWWO, Log, TEXT("Anchor grafts follow %s's LODs, which start at screen sizes%s (r.SkeletalMeshLODBias %d): with its bounds' radius of %.0f cm, on 16:9 %s"),
			*Mesh->GetName(), *Sizes, Bias ? Bias->GetInt() : 0, Radius, *Dists);
	}
	// Following the robot's LOD: checked after every pose update (as cheap as reading a number).
	AnchorLodModel = Model;
	AnchorLodHandle = Model->RegisterOnBoneTransformsFinalizedDelegate(FOnBoneTransformsFinalizedMultiCast::FDelegate::CreateUObject(this, &UWWOBodyComponent::UpdateAnchorLevel));
	UE_LOG(LogWWO, Log, TEXT("Body %s: anchor grafts given (%s): %d meshes + %s, triangles %d / %d / %d by level, now level %d; asked in %.2f ms (the dearest ask), given in %.2f ms"),
		*GetOwner()->GetName(), *How, Result.Meshes.Num(), Result.Joints ? TEXT("joints") : TEXT("no joints"), Result.Tris[0], Result.Tris[1], Result.Tris[2],
		AnchorLevel, AnchorAskedMs, (FPlatformTime::Seconds() - Started) * 1000.0);
	AnchorAskedMs = 0.0;
}
