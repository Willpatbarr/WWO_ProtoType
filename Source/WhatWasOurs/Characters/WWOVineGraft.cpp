#include "Characters/WWOVineGraft.h"
#include "Characters/WWOShellAnchors.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Materials/MaterialInterface.h"
#include "Engine/StaticMesh.h"
#include "StaticMeshResources.h"
#include "FX/WWOFX.h"
#include "Misc/ScopeLock.h"
#include "WhatWasOurs.h"
#include "WWOSwitches.h"

namespace
{
	const FVector Up(0.f, 0.f, 1.f);
	const FVector Front(0.f, 1.f, 0.f); // the robot faces +Y in its mesh's space

	// The crystal's glow in the VoltVine atlas (T_voltvine_atlas, shelf-packed by the Blender build, graft_tex.Atlas):
	// its rectangle in UV (its tip at V0).
	struct FIsland { float U0, V0, U1, V1; };
	const FIsland CrystalIsland = { 0.6987f, 0.1030f, 0.8516f, 0.3779f };

	const TCHAR* MaterialPaths[FWWOVineMesh::NumSections] = {
		TEXT("/Game/WWO/Flora/VoltVine/Materials/MI_VV_Bark.MI_VV_Bark"), TEXT("/Game/WWO/Flora/VoltVine/Materials/MI_VV_Core.MI_VV_Core"),
		TEXT("/Game/WWO/Flora/VoltVine/Materials/MI_VV_Leaf.MI_VV_Leaf"), TEXT("/Game/WWO/Flora/VoltVine/Materials/MI_VV_Crystal.MI_VV_Crystal"),
		TEXT("/Game/WWO/Flora/VoltVine/Materials/MI_VV_Stem.MI_VV_Stem"),
		TEXT("/Game/WWO/Flora/VeilSpore/Materials/MI_VS_Fungus.MI_VS_Fungus"), // VeilSpore's atlas (Tools/import_robot_grafts.py)
		TEXT("M_WWO_GraftBark"), TEXT("M_WWO_GraftSeed"), TEXT("M_WWO_GraftVine") }; // FWWOGraftBuilder's (WWOFX::Material)

	// VeilSpore's atlas (T_veilspore_atlas, ~/Documents/WWO_Flora/kit_veilspore/kit.json islands_uv, turned to Unreal's V
	// down): 'net', the mycelium's lookup: U thin (violet) to thick (pale lavender), V glowing (top) to not.
	const FIsland NetIsland = { 0.7334f + 0.004f, 1.f - 0.7607f + 0.004f, 0.8120f - 0.004f, 1.f - 0.6821f - 0.004f };

	constexpr int32 JointHalf = 4;   // stations (~5 cm) either side of a change of bone posed between the two
	constexpr int32 MinRun = 4;      // a stretch on one bone shorter than this joins its neighbour
	constexpr int32 NumShapeLods = 4; // the copied pieces' LODs (Tools/make_graft_piece_lods.py)
	constexpr int32 LeafCard = 4;    // FWWOGraftDetail::LeafLod: a flat card instead of a mesh LOD

	float Smooth(float A, float B, float X)
	{
		const float T = FMath::Clamp((X - A) / FMath::Max(B - A, UE_SMALL_NUMBER), 0.f, 1.f);
		return T * T * (3.f - 2.f * T);
	}

	FVector Unit(const FVector& V, const FVector& Fallback = Up) { return V.GetSafeNormal(UE_SMALL_NUMBER, Fallback); }

	float Gauss(FRandomStream& R, float Sigma)
	{
		const float U1 = FMath::Max(R.FRand(), 1e-6f), U2 = R.FRand();
		return Sigma * FMath::Sqrt(-2.f * FMath::Loge(U1)) * FMath::Cos(UE_TWO_PI * U2);
	}

	FVector Gauss3(FRandomStream& R, float Sigma) { return FVector(Gauss(R, Sigma), Gauss(R, Sigma), Gauss(R, Sigma)); }

	// Each point's direction along a line.
	TArray<FVector> Tangents(const TArray<FVector>& P)
	{
		TArray<FVector> T;
		const int32 Last = P.Num() - 1;
		T.SetNum(P.Num());
		for (int32 i = 0; i <= Last; ++i) T[i] = Unit(P[FMath::Min(i + 1, Last)] - P[FMath::Max(i - 1, 0)], Front);
		return T;
	}

	// Frames square to a line, carried along it without twisting (the first one's F1 toward Hint).
	void Frames(const TArray<FVector>& P, const FVector& Hint, TArray<FVector>& F1, TArray<FVector>& F2)
	{
		const TArray<FVector> T = Tangents(P);
		F1.SetNum(P.Num());
		F2.SetNum(P.Num());
		FVector N = Hint;
		for (int32 i = 0; i < P.Num(); ++i)
		{
			FVector Q = N - T[i] * FVector::DotProduct(N, T[i]);
			if (!Q.Normalize()) Q = Unit(FVector::CrossProduct(T[i], FMath::Abs(T[i].Z) < 0.9f ? Up : Front));
			N = Q;
			F1[i] = N;
			F2[i] = FVector::CrossProduct(T[i], N); // F1 x F2 = T: a ring round them faces out
		}
	}

	// A static mesh's LOD as a shape (it must keep its vertices on the CPU: allow_cpu_access on import). A mesh with
	// fewer LODs gives its last.
	TSharedPtr<FWWOLeafShape> ReadShape(const UStaticMesh* SM, const FString& Path, int32 Lod)
	{
		const FStaticMeshRenderData* RD = SM ? SM->GetRenderData() : nullptr;
		if (RD) Lod = FMath::Clamp(Lod, 0, RD->LODResources.Num() - 1);
		if (!RD || RD->LODResources.Num() == 0 || RD->LODResources[Lod].VertexBuffers.PositionVertexBuffer.GetNumVertices() == 0)
		{
			UE_LOG(LogWWO, Warning, TEXT("Vine grafts: no mesh (or no CPU copy of it) %s LOD %d"), *Path, Lod);
			return nullptr;
		}
		const FStaticMeshLODResources& LOD = RD->LODResources[Lod];
		const FStaticMeshVertexBuffers& VB = LOD.VertexBuffers;
		TSharedPtr<FWWOLeafShape> Shape = MakeShared<FWWOLeafShape>();
		for (uint32 v = 0; v < VB.PositionVertexBuffer.GetNumVertices(); ++v)
		{
			Shape->V.Add(FVector(VB.PositionVertexBuffer.VertexPosition(v)));
			Shape->N.Add(FVector(FVector3f(VB.StaticMeshVertexBuffer.VertexTangentZ(v))));
			Shape->T.Add(FVector(FVector3f(VB.StaticMeshVertexBuffer.VertexTangentX(v))));
			Shape->UV.Add(FVector2D(VB.StaticMeshVertexBuffer.GetVertexUV(v, 0)));
			Shape->Length = FMath::Max(Shape->Length, float(Shape->V.Last().X));
			Shape->Bounds += Shape->V.Last();
		}
		TArray<uint32> Indices;
		LOD.IndexBuffer.GetCopy(Indices);
		for (const FStaticMeshSection& Sec : LOD.Sections)
		{
			const TArray<FStaticMaterial>& Mats = SM->GetStaticMaterials();
			const bool bStem = Mats.IsValidIndex(Sec.MaterialIndex) && Mats[Sec.MaterialIndex].MaterialSlotName == FName(TEXT("VV_Stem"));
			for (uint32 k = Sec.FirstIndex; k < Sec.FirstIndex + Sec.NumTriangles * 3 && int32(k) < Indices.Num(); ++k) Shape->Tris[bStem ? 1 : 0].Add(int32(Indices[k]));
		}
		if (Shape->Tris[0].Num() == 0) return nullptr;
		return Shape;
	}

	// A leaf as a flat card (the farthest level): a diamond from its stalk's base to its tip, as wide as the leaf
	// (its two triangles in the blade's section), in one color: the leaf's own UV half way between its middle and its
	// edge (its corners' own UVs are on the glowing midrib and the rim: a card of those reads cyan).
	TSharedPtr<FWWOLeafShape> LeafCardOf(const FWWOLeafShape& Leaf)
	{
		int32 Base = 0, Tip = 0, Left = 0, Right = 0;
		FVector Middle = FVector::ZeroVector;
		for (int32 v = 0; v < Leaf.V.Num(); ++v)
		{
			if (Leaf.V[v].X < Leaf.V[Base].X) Base = v;
			if (Leaf.V[v].X > Leaf.V[Tip].X) Tip = v;
			if (Leaf.V[v].Y > Leaf.V[Left].Y) Left = v;
			if (Leaf.V[v].Y < Leaf.V[Right].Y) Right = v;
			Middle += Leaf.V[v];
		}
		Middle /= FMath::Max(Leaf.V.Num(), 1);
		const FVector Blade = FMath::Lerp(Middle, Leaf.V[Left], 0.5f);
		int32 Colour = 0;
		for (int32 v = 0; v < Leaf.V.Num(); ++v) if (FVector::DistSquared(Leaf.V[v], Blade) < FVector::DistSquared(Leaf.V[Colour], Blade)) Colour = v;
		TSharedPtr<FWWOLeafShape> Card = MakeShared<FWWOLeafShape>();
		for (const int32 v : { Base, Left, Tip, Right })
		{
			Card->V.Add(Leaf.V[v]);
			Card->N.Add(FVector(0.f, 0.f, 1.f));
			Card->T.Add(FVector(1.f, 0.f, 0.f));
			Card->UV.Add(Leaf.UV[Colour]);
			Card->Bounds += Leaf.V[v];
		}
		Card->Length = Leaf.Length;
		// (Unreal's draw order, as the meshes' own: its front faces +Z.)
		Card->Tris[0] = { 0, 2, 1, 0, 3, 2 };
		const FVector Face = FVector::CrossProduct(Card->V[1] - Card->V[0], Card->V[2] - Card->V[0]);
		if (Face.Z > 0.f) Card->Tris[0] = { 0, 1, 2, 0, 2, 3 };
		return Card;
	}

	// The copied shapes, read once a run, by path and LOD (and the leaves'), on the build threads, from meshes the game
	// thread has loaded (FWWOAnchorGraftCache); never changed once read.
	FCriticalSection ShapesLock;
	TMap<FString, TSharedPtr<FWWOLeafShape>> ShapeCache; // "path|lod"
	TArray<TSharedPtr<FWWOLeafShape>> LeafCache;      // kind x (LODs 0-3, the card)
	bool bLeavesRead = false;
	const TCHAR* LeafNames[] = { TEXT("01"), TEXT("02"), TEXT("03"), TEXT("04"), TEXT("06") }; // (SM_VV_Leaf05, the young one, left out)

	// A triangle whose front face is the side (B - A) x (C - A) points to (Unreal draws (P2 - P0) x (P1 - P0)).
	void Tri(TArray<int32>& Tris, int32 A, int32 B, int32 C)
	{
		Tris.Add(A);
		Tris.Add(C);
		Tris.Add(B);
	}
}

// --- Detail levels -------------------------------------------------------------------------------

FWWOGraftDetail FWWOGraftDetail::Default(int32 Level)
{
	FWWOGraftDetail D;
	if (Level <= 0)
	{
		// Close: as built before levels, but a ring every other station (2.4 cm: the strands' twist and the line's bends
		// stay round), the reduced leaf mesh (~30%) on a lighter shoot and stalk, the small pieces' rings halved, the
		// mycelium's too (its thin strands three-sided), and the kit's small pieces (under 9 cm across) from their ~30% mesh.
		D.Step = 2; D.CoreStep = 4;
		D.LeafLod = 1;
		D.TwigSides = 5; D.TwigStep = 2;
		D.PetioleSides = 3; D.PetioleStep = 6;
		D.SmallStep = 2;
		D.KitSmall = 9.f; D.KitSmallLod = 1;
		D.NetThinSides = 3; D.NetStep = 2; D.KnobSides = 4; D.KnobRings = 3;
		return D;
	}
	if (Level == 1)
	{
		// ~25%: four-sided strands every fourth station, 70% of the leaves from their ~14% mesh on a bare shoot, coarser
		// pieces, the kit's ~30% meshes, a thinner mycelium without its knobs.
		D.StrandSides = 4; D.CoreSides = 3; D.WindSides = 3; D.Step = 4; D.CoreStep = 6;
		D.LeafLod = 2; D.LeafKeep = 0.7f; D.TwigSides = 3; D.TwigStep = 4; D.PetioleSides = 0;
		D.SmallSides = 3; D.SmallStep = 4;
		D.CrystalRings = 5; D.CrystalAround = 6;
		D.KitLod = 1;
		D.NetSides = 3; D.NetThinSides = 3; D.NetStep = 3; D.KnobSides = 0;
		D.PlateRows = 3; D.PlateCols = 2; D.PodSides = 6; D.PodRings = 5;
		return D;
	}
	// ~5%: one rope tube, under half the leaves as flat cards, no small pieces, the kit's pieces over 12 cm from their
	// ~4% meshes, only the mycelium's thick strands.
	D.bStrands = false; D.RopeSides = 4; D.Step = 4;
	D.LeafLod = LeafCard; D.LeafKeep = 0.45f; D.TwigSides = 0; D.PetioleSides = 0;
	D.bSmall = false;
	D.CrystalRings = 3; D.CrystalAround = 4;
	D.KitLod = 3; D.KitMin = 12.f;
	D.NetSides = 3; D.NetThinSides = 3; D.NetStep = 4; D.NetMinRadius = 0.45f; D.KnobSides = 0;
	D.PlateRows = 2; D.PlateCols = 1; D.PodSides = 4; D.PodRings = 3; D.bPodStalk = false;
	return D;
}

// --- FWWOVineMesh --------------------------------------------------------------------------------

const FWWOLeafShape* FWWOVineGraft::LeafShape(int32 Kind, int32 Lod)
{
	{
		FScopeLock Lock(&ShapesLock);
		if (!bLeavesRead) return nullptr;
	}
	const int32 i = Kind * (NumShapeLods + 1) + FMath::Clamp(Lod, 0, NumShapeLods);
	return LeafCache.IsValidIndex(i) ? LeafCache[i].Get() : nullptr;
}

TArray<FString> FWWOVineGraft::LeafPaths()
{
	TArray<FString> Out;
	for (const TCHAR* N : LeafNames) Out.Add(FString::Printf(TEXT("/Game/WWO/Characters/RobotFrame/Grafts/Pieces/SM_VV_Leaf%s.SM_VV_Leaf%s"), N, N));
	return Out;
}

void FWWOVineGraft::ReadLeaves(TConstArrayView<const UStaticMesh*> Meshes)
{
	FScopeLock Lock(&ShapesLock);
	if (bLeavesRead) return;
	bLeavesRead = true;
	const TArray<FString> Paths = LeafPaths();
	for (int32 k = 0; k < Paths.Num(); ++k)
	{
		const UStaticMesh* SM = Meshes.IsValidIndex(k) ? Meshes[k] : nullptr;
		for (int32 Lod = 0; Lod < NumShapeLods; ++Lod)
		{
			TSharedPtr<FWWOLeafShape> Shape = SM ? ReadShape(SM, Paths[k], Lod) : nullptr;
			if (Shape && Shape->Length <= 1.f) Shape.Reset();
			LeafCache.Add(Shape);
		}
		LeafCache.Add(LeafCache[LeafCache.Num() - NumShapeLods] ? LeafCardOf(*LeafCache[LeafCache.Num() - NumShapeLods]) : nullptr);
	}
}

bool FWWOVineMesh::IsEmpty() const
{
	for (const FSection& Sec : S) if (Sec.Tris.Num() > 0) return false;
	return true;
}

int32 FWWOVineMesh::NumTris() const
{
	int32 N = 0;
	for (const FSection& Sec : S) N += Sec.Tris.Num() / 3;
	return N;
}

FString FWWOVineMesh::MaterialPath(ESection Section)
{
	if (Section >= GraftBark) return FString::Printf(TEXT("/Game/WWO/Materials/%s.%s"), MaterialPaths[Section], MaterialPaths[Section]); // (as WWOFX::Material)
	return MaterialPaths[Section];
}

UMaterialInterface* FWWOVineMesh::Material(ESection Section)
{
	if (Section >= GraftBark) return WWOFX::Material(MaterialPaths[Section]);
	return LoadObject<UMaterialInterface>(nullptr, MaterialPaths[Section]);
}

void FWWOVineGraft::ReadMesh(const FString& ObjectPath, const UStaticMesh* Mesh)
{
	FScopeLock Lock(&ShapesLock);
	if (ShapeCache.Contains(ObjectPath + TEXT("|0"))) return;
	for (int32 Lod = 0; Lod < NumShapeLods; ++Lod) ShapeCache.Add(FString::Printf(TEXT("%s|%d"), *ObjectPath, Lod), Mesh ? ReadShape(Mesh, ObjectPath, Lod) : nullptr);
}

const FWWOLeafShape* FWWOVineGraft::MeshShape(const FString& ObjectPath, int32 Lod)
{
	FScopeLock Lock(&ShapesLock);
	const TSharedPtr<FWWOLeafShape>* Found = ShapeCache.Find(FString::Printf(TEXT("%s|%d"), *ObjectPath, FMath::Clamp(Lod, 0, NumShapeLods - 1)));
	return Found ? Found->Get() : nullptr;
}

// --- FWWOVineJoints ------------------------------------------------------------------------------

FWWOVineJoints::FWWOVineJoints(const FWWOVineJoints& Other)
	: Pairs(Other.Pairs), Stations(Other.Stations), Level(Other.Level)
{
	for (int32 L = 0; L < NumGraftLevels; ++L)
	{
		for (int32 s = 0; s < FWWOVineMesh::NumSections; ++s) S[L][s] = Other.S[L][s];
	}
}

int32 FWWOVineJoints::NumTris(int32 InLevel) const
{
	int32 N = 0;
	for (const FSection& Sec : S[FMath::Clamp(InLevel, 0, NumGraftLevels - 1)]) N += Sec.Tris.Num() / 3;
	return N;
}

void FWWOVineJoints::CreateSections()
{
	UProceduralMeshComponent* C = Comp.Get();
	if (!C) return;
	C->ClearAllMeshSections();
	SectionOf.Reset();
	for (int32 s = 0; s < FWWOVineMesh::NumSections; ++s)
	{
		FSection& Sec = S[Level][s];
		if (Sec.Tris.Num() == 0) continue;
		Sec.V = Sec.RefV;
		Sec.N = Sec.RefN;
		Sec.T = Sec.RefT;
		C->CreateMeshSection(SectionOf.Num(), Sec.RefV, Sec.Tris, Sec.RefN, Sec.UV, TArray<FColor>(), Sec.RefT, false);
		C->SetMaterial(SectionOf.Num(), FWWOVineMesh::Material(FWWOVineMesh::ESection(s)));
		SectionOf.Add(s);
	}
}

void FWWOVineJoints::Start(UProceduralMeshComponent* C, USkeletalMeshComponent* InModel, int32 InLevel)
{
	Comp = C;
	Model = InModel;
	Level = FMath::Clamp(InLevel, 0, NumGraftLevels - 1);
	CreateSections();
	if (InModel)
	{
		Handle = InModel->RegisterOnBoneTransformsFinalizedDelegate(FOnBoneTransformsFinalizedMultiCast::FDelegate::CreateRaw(this, &FWWOVineJoints::Update));
	}
	Update();
}

void FWWOVineJoints::SetLevel(int32 InLevel)
{
	InLevel = FMath::Clamp(InLevel, 0, NumGraftLevels - 1);
	if (InLevel == Level) return;
	Level = InLevel;
	CreateSections();
	const int32 Was = Updates;
	Updates = 0; // (posed now, even if not drawn lately)
	Update();
	Updates += Was;
}

void FWWOVineJoints::Report() const
{
	if (const USkeletalMeshComponent* M = Model.Get(); M && Updates > 0)
	{
		// What posing them between their bones saved: how far apart each side's end would have moved if rigid.
		FString Widest;
		TArray<int32> Order;
		for (int32 i = 0; i < WidestSplit.Num(); ++i) Order.Add(i);
		Order.Sort([&](int32 A, int32 B) { return WidestSplit[A] > WidestSplit[B]; });
		for (int32 k = 0; k < Order.Num(); ++k)
		{
			const FPair& Pr = Pairs[Order[k]];
			Widest += FString::Printf(TEXT(" %s-%s %.1f"), *M->GetBoneName(Pr.A).ToString(), *M->GetBoneName(Pr.B).ToString(), WidestSplit[Order[k]]);
		}
		UE_LOG(LogWWO, Log, TEXT("Vine joints on %s: %d stations posed %d times, %.3f ms each; the widest gaps a rigid split would have opened (cm):%s"),
			*M->GetOwner()->GetName(), Stations.Num(), Updates, Seconds * 1000.0 / Updates, *Widest);
	}
}

void FWWOVineJoints::Stop()
{
	Report();
	if (USkeletalMeshComponent* M = Model.Get(); M && Handle.IsValid()) M->UnregisterOnBoneTransformsFinalizedDelegate(Handle);
	Handle.Reset();
	Model = nullptr;
}

void FWWOVineJoints::Update()
{
	USkeletalMeshComponent* M = Model.Get();
	UProceduralMeshComponent* C = Comp.Get();
	if (!M || !C || !C->IsRegistered()) return;
	// Not drawn for a while (off screen, behind the camera, far away): left as it is until it is seen again (a
	// frame late then). Your own body's are always posed: only their shadow is drawn.
	if (Updates > 0 && !C->bCastHiddenShadow && !C->WasRecentlyRendered(0.5f)) return;
	const double Started = FPlatformTime::Seconds();
	const TArray<FTransform>& CS = M->GetComponentSpaceTransforms();
	// Each bone's move from the reference pose (a point in the mesh's reference space to where it is now).
	TArray<FTransform, TInlineAllocator<32>> DA, DB;
	for (const FPair& Pr : Pairs)
	{
		if (!CS.IsValidIndex(Pr.A) || !CS.IsValidIndex(Pr.B)) return;
		DA.Add(Pr.RefA.Inverse() * CS[Pr.A]);
		DB.Add(Pr.RefB.Inverse() * CS[Pr.B]);
	}
	WidestSplit.SetNumZeroed(Pairs.Num());
	TArray<FVector, TInlineAllocator<256>> Pos;
	TArray<FQuat, TInlineAllocator<256>> Rot;
	Pos.SetNum(Stations.Num());
	Rot.SetNum(Stations.Num());
	for (int32 i = 0; i < Stations.Num(); ++i)
	{
		const FStation& St = Stations[i];
		const FVector PA = DA[St.Pair].TransformPosition(St.C), PB = DB[St.Pair].TransformPosition(St.C);
		Pos[i] = FMath::Lerp(PA, PB, St.W);
		if (St.W > 0.3f && St.W < 0.7f) WidestSplit[St.Pair] = FMath::Max(WidestSplit[St.Pair], float(FVector::Dist(PA, PB)));
		Rot[i] = FQuat::Slerp(DA[St.Pair].GetRotation(), DB[St.Pair].GetRotation(), St.W).GetNormalized();
	}
	for (int32 k = 0; k < SectionOf.Num(); ++k)
	{
		FSection& Sec = S[Level][SectionOf[k]];
		for (int32 v = 0; v < Sec.RefV.Num(); ++v)
		{
			const int32 i = Sec.Station[v];
			const FQuat& Q = Rot[i];
			Sec.V[v] = Pos[i] + Q.RotateVector(Sec.RefV[v] - Stations[i].C);
			Sec.N[v] = Q.RotateVector(Sec.RefN[v]);
			Sec.T[v].TangentX = Q.RotateVector(Sec.RefT[v].TangentX);
		}
		C->UpdateMeshSection(k, Sec.V, Sec.N, Sec.UV, TArray<FColor>(), Sec.T);
	}
	Seconds += FPlatformTime::Seconds() - Started;
	if (++Updates % 1000 == 0 && WWOSwitches::Has(TEXT("WWOAutoTestBody"))) Report(); // (the body test's run, for its log)
}

// --- FWWOVineLine --------------------------------------------------------------------------------

void FWWOVineLine::Add(const FVector& InP, const FVector& InN, FName InBone, float InThick, float InLeafScale, float InLeafy)
{
	P.Add(InP);
	N.Add(InN);
	Bone.Add(InBone);
	Thick.Add(InThick);
	LeafScale.Add(InLeafScale);
	Leafy.Add(InLeafy);
}

FWWOVineLine FWWOVineGraft::Resample(const FWWOVineLine& In, float Step)
{
	FWWOVineLine Out;
	const int32 Num = In.Num();
	if (Num < 2) return In;
	TArray<float> At;
	At.Add(0.f);
	for (int32 i = 1; i < Num; ++i) At.Add(At.Last() + FVector::Dist(In.P[i - 1], In.P[i]));
	const float Total = At.Last();
	const int32 Count = FMath::Max(2, FMath::RoundToInt(Total / Step) + 1);
	int32 Seg = 0;
	for (int32 k = 0; k < Count; ++k)
	{
		const float S = Total * k / (Count - 1);
		while (Seg + 2 < Num && At[Seg + 1] < S) ++Seg;
		const float Len = At[Seg + 1] - At[Seg];
		const float T = Len > UE_SMALL_NUMBER ? FMath::Clamp((S - At[Seg]) / Len, 0.f, 1.f) : 0.f;
		const int32 Near = T < 0.5f ? Seg : Seg + 1;
		Out.Add(FMath::Lerp(In.P[Seg], In.P[Seg + 1], T), Unit(FMath::Lerp(In.N[Seg], In.N[Seg + 1], T), In.N[Near]), In.Bone[Near],
			FMath::Lerp(In.Thick[Seg], In.Thick[Seg + 1], T), FMath::Lerp(In.LeafScale[Seg], In.LeafScale[Seg + 1], T),
			FMath::Lerp(In.Leafy[Seg], In.Leafy[Seg + 1], T));
	}
	// What the recipe set per stretch changes gradually (over ~10 cm), not at the join.
	for (TArray<float>* A : { &Out.Thick, &Out.LeafScale, &Out.Leafy })
	{
		const TArray<float> Raw = *A;
		for (int32 i = 0; i < Raw.Num(); ++i)
		{
			float Sum = 0.f;
			int32 Used = 0;
			for (int32 j = FMath::Max(0, i - 4); j <= FMath::Min(Raw.Num() - 1, i + 4); ++j, ++Used) Sum += Raw[j];
			(*A)[i] = Sum / Used;
		}
	}
	return Out;
}

void FWWOVineGraft::BoneWrap(const USkeletalMesh* InMesh, FName Bone, FName ToBone, float From, float To, float Turns, float Phase,
	const FVector2D& Radii, const FVector& Outward, FWWOVineLine& Out)
{
	const FVector A = WWOShellAnchors::RefBone(InMesh, Bone).GetLocation(), B = WWOShellAnchors::RefBone(InMesh, ToBone).GetLocation();
	const FVector Axis = B - A, Ax = Unit(Axis);
	const FVector Out0 = Unit(Outward - Ax * FVector::DotProduct(Outward, Ax));
	const FVector Side = Unit(FVector::CrossProduct(Ax, Out0));
	const int32 Count = FMath::Max(8, int32(24 + 22 * Turns));
	for (int32 i = 0; i < Count; ++i)
	{
		const float F = float(i) / (Count - 1);
		const float Angle = Phase + UE_TWO_PI * Turns * F;
		const float C = FMath::Cos(Angle), S = FMath::Sin(Angle);
		const FVector P = A + Axis * FMath::Lerp(From, To, F) + Out0 * C * Radii.X + Side * S * Radii.Y;
		Out.Add(P, Unit(Out0 * C / Radii.X + Side * S / Radii.Y), Bone, 1.f, 1.f, 1.f);
	}
}

// --- FWWOVineGraft -------------------------------------------------------------------------------

FWWOVineGraft::FWWOVineGraft(const USkeletalMesh* InMesh, FRandomStream& InRandom, bool bInJoints, const FWWOGraftDetail (&InDetail)[NumGraftLevels])
	: Joints(MakeShared<FWWOVineJoints>()), Mesh(InMesh), Random(InRandom), bJoints(bInJoints)
{
	for (int32 L = 0; L < NumGraftLevels; ++L) Detail[L] = InDetail[L];
}

FWWOVineGraft::FRings FWWOVineGraft::SmallRings(int32 Sides0) const
{
	FRings R;
	for (int32 L = 0; L < NumGraftLevels; ++L)
	{
		R.Sides[L] = Detail[L].bSmall ? FMath::Min(Sides0, Detail[L].SmallSides) : 0;
		R.Step[L] = Detail[L].SmallStep;
	}
	return R;
}

FName FWWOVineGraft::BoneAt(int32 Station) const
{
	const FStationOwner& O = Owners[Station];
	if (O.Pair == INDEX_NONE) return O.Bone;
	const FWWOVineJoints::FPair& Pr = Joints->Pairs[O.Pair];
	return Mesh->GetRefSkeleton().GetBoneName(O.W < 0.5f ? Pr.A : Pr.B);
}

void FWWOVineGraft::PlanJoints(const FWWOVineLine& Line, const TArray<FVector>& C)
{
	const int32 Num = Line.Num();
	// The stretches on one bone, the short ones (a point or two that the surface gave a neighbouring bone) merged.
	struct FRun { FName Bone; int32 Start, End; };
	TArray<FRun> Runs;
	for (int32 i = 0; i < Num; ++i)
	{
		if (Runs.Num() && Runs.Last().Bone == Line.Bone[i]) Runs.Last().End = i;
		else Runs.Add({ Line.Bone[i], i, i });
	}
	for (bool bMerged = true; bMerged && Runs.Num() > 1;)
	{
		bMerged = false;
		for (int32 r = 0; r < Runs.Num(); ++r)
		{
			if (Runs[r].End - Runs[r].Start + 1 >= MinRun) continue;
			const int32 Into = r > 0 ? r - 1 : r + 1;
			Runs[Into].Start = FMath::Min(Runs[Into].Start, Runs[r].Start);
			Runs[Into].End = FMath::Max(Runs[Into].End, Runs[r].End);
			Runs.RemoveAt(r);
			for (int32 k = Runs.Num() - 1; k > 0; --k) // neighbours on the same bone become one
			{
				if (Runs[k].Bone == Runs[k - 1].Bone) { Runs[k - 1].End = Runs[k].End; Runs.RemoveAt(k); }
			}
			bMerged = true;
			break;
		}
	}
	Owners.SetNum(Num);
	for (const FRun& R : Runs) for (int32 i = R.Start; i <= R.End; ++i) Owners[i] = { R.Bone, INDEX_NONE, 0.f };
	if (!bJoints) return;
	const FReferenceSkeleton& Ref = Mesh->GetRefSkeleton();
	for (int32 r = 0; r + 1 < Runs.Num(); ++r)
	{
		const int32 B = Runs[r + 1].Start;
		const int32 K0 = FMath::Max(B - JointHalf, (Runs[r].Start + B) / 2);
		const int32 K1 = FMath::Min(B + JointHalf, (B + Runs[r + 1].End) / 2);
		const int32 IA = Ref.FindBoneIndex(Runs[r].Bone), IB = Ref.FindBoneIndex(Runs[r + 1].Bone);
		if (K1 - K0 < 2 || IA == INDEX_NONE || IB == INDEX_NONE) continue;
		int32 Pair = Joints->Pairs.IndexOfByPredicate([&](const FWWOVineJoints::FPair& P) { return P.A == IA && P.B == IB; });
		if (Pair == INDEX_NONE)
		{
			FWWOVineJoints::FPair& P = Joints->Pairs.AddDefaulted_GetRef();
			P.A = IA;
			P.B = IB;
			P.RefA = WWOShellAnchors::RefBone(Mesh, Runs[r].Bone);
			P.RefB = WWOShellAnchors::RefBone(Mesh, Runs[r + 1].Bone);
			Pair = Joints->Pairs.Num() - 1;
		}
		for (int32 i = K0; i <= K1; ++i) Owners[i] = { i < B ? Runs[r].Bone : Runs[r + 1].Bone, Pair, Smooth(float(K0), float(K1), float(i)) };
	}
}

void FWWOVineGraft::Tube(FWWOVineMesh::ESection Section, const TArray<FVector>& Path, const TArray<float>& R, const TArray<FVector>& F1,
	const TArray<FVector>& F2, const FRings& Rings, float VScale, const TArray<FVector2D>* RingUV)
{
	const int32 Num = Path.Num();
	if (Num < 2) return;
	TArray<float> At;
	At.Add(0.f);
	for (int32 i = 1; i < Num; ++i) At.Add(At.Last() + FVector::Dist(Path[i - 1], Path[i]));
	// Each span (i, i + 1) is in the joint section when both its stations are in the same one, else rigid on a bone.
	TArray<bool> SpanJoint;
	TArray<FName> SpanBone;
	for (int32 i = 0; i + 1 < Num; ++i)
	{
		SpanJoint.Add(Owners[i].Pair != INDEX_NONE && Owners[i].Pair == Owners[i + 1].Pair);
		SpanBone.Add(Owners[i].Pair == INDEX_NONE || Owners[i + 1].Pair != INDEX_NONE ? Owners[i].Bone : Owners[i + 1].Bone);
	}
	auto Same = [&](int32 a, int32 b) { return SpanJoint[a] == SpanJoint[b] && (SpanJoint[a] || SpanBone[a] == SpanBone[b]); };
	for (int32 L = 0; L < NumGraftLevels; ++L)
	{
		const int32 Sides = Rings.Sides[L];
		if (Sides < 3) continue;
		// The rings this level keeps: every Step-th station, the ends, and every station where a span changes owner (so
		// each kept span lies wholly in one owner's mesh).
		const int32 Step = FMath::Max(Rings.Step[L], 1);
		TArray<int32> Keep;
		for (int32 i = 0; i < Num; ++i)
		{
			if (i == 0 || i == Num - 1 || i % Step == 0 || (i + 1 < Num && !Same(i, i - 1))) Keep.Add(i);
		}
		int32 k = 0;
		while (k + 1 < Keep.Num())
		{
			const int32 First = Keep[k];
			int32 e = k + 1;
			while (e + 1 < Keep.Num() && Same(Keep[e], First)) ++e;
			// Rings Keep[k..e] into one mesh.
			const bool bJoint = SpanJoint[First];
			TArray<FVector>* V;
			TArray<FVector>* Nm;
			TArray<FVector2D>* UV;
			TArray<FProcMeshTangent>* Tg;
			TArray<int32>* Tris;
			TArray<int32>* Station = nullptr;
			if (bJoint)
			{
				FWWOVineJoints::FSection& Sec = Joints->S[L][Section];
				V = &Sec.RefV; Nm = &Sec.RefN; UV = &Sec.UV; Tg = &Sec.RefT; Tris = &Sec.Tris; Station = &Sec.Station;
			}
			else
			{
				FWWOVineMesh::FSection& Sec = MeshOn(SpanBone[First], L).S[Section];
				V = &Sec.V; Nm = &Sec.N; UV = &Sec.UV; Tg = &Sec.T; Tris = &Sec.Tris;
			}
			const int32 Base = V->Num();
			for (int32 q = k; q <= e; ++q)
			{
				const int32 i = Keep[q];
				for (int32 j = 0; j <= Sides; ++j)
				{
					const float A = UE_TWO_PI * j / Sides;
					const FVector Dir = F1[i] * FMath::Cos(A) + F2[i] * FMath::Sin(A);
					V->Add(Path[i] + Dir * R[i]);
					Nm->Add(Dir);
					UV->Add(RingUV ? (*RingUV)[i] : FVector2D(float(j) / Sides, At[i] * VScale));
					Tg->Add(FProcMeshTangent(-F1[i] * FMath::Sin(A) + F2[i] * FMath::Cos(A), false));
					if (Station) Station->Add(StationIds[i]);
				}
			}
			for (int32 i = 0; i < e - k; ++i)
			{
				for (int32 j = 0; j < Sides; ++j)
				{
					const int32 A = Base + i * (Sides + 1) + j, B = A + 1, D = A + Sides + 1, C = D + 1;
					Tri(*Tris, A, B, D);
					Tri(*Tris, B, C, D);
				}
			}
			k = e;
		}
	}
}

void FWWOVineGraft::Strip(FName Bone, FWWOVineMesh::ESection Section, const TArray<FVector>& Path, const TArray<float>& R, const FRings& Rings)
{
	// A small tube (a tendril, a cradle, a thorn's arc) all on one bone.
	if (Path.Num() < 2) return;
	TArray<FVector> F1, F2;
	Frames(Path, Unit(FVector::CrossProduct(Path.Last() - Path[0], Up), Front), F1, F2);
	TArray<float> At;
	At.Add(0.f);
	for (int32 i = 1; i < Path.Num(); ++i) At.Add(At.Last() + FVector::Dist(Path[i - 1], Path[i]));
	for (int32 L = 0; L < NumGraftLevels; ++L)
	{
		const int32 Sides = Rings.Sides[L];
		if (Sides < 3) continue;
		const int32 Step = FMath::Max(Rings.Step[L], 1);
		TArray<int32> Keep;
		for (int32 i = 0; i < Path.Num(); ++i) if (i % Step == 0 || i == Path.Num() - 1) Keep.Add(i);
		FWWOVineMesh::FSection& Sec = MeshOn(Bone, L).S[Section];
		const int32 Base = Sec.V.Num();
		for (const int32 i : Keep)
		{
			for (int32 j = 0; j <= Sides; ++j)
			{
				const float A = UE_TWO_PI * j / Sides;
				const FVector Dir = F1[i] * FMath::Cos(A) + F2[i] * FMath::Sin(A);
				Sec.V.Add(Path[i] + Dir * R[i]);
				Sec.N.Add(Dir);
				Sec.UV.Add(FVector2D(float(j) / Sides, At[i] * 0.4f));
				Sec.T.Add(FProcMeshTangent(-F1[i] * FMath::Sin(A) + F2[i] * FMath::Cos(A), false));
			}
		}
		for (int32 i = 0; i + 1 < Keep.Num(); ++i)
		{
			for (int32 j = 0; j < Sides; ++j)
			{
				const int32 A = Base + i * (Sides + 1) + j, B = A + 1, D = A + Sides + 1, C = D + 1;
				Tri(Sec.Tris, A, B, D);
				Tri(Sec.Tris, B, C, D);
			}
		}
	}
}

void FWWOVineGraft::Vine(const FWWOVineLine& Line, const FWWOVineStyle& Style)
{
	const int32 Num = Line.Num();
	if (Num < 4) return;
	const float Seed = Random.FRand() * 10.f;
	// The rope's middle: off the surface by its own thickness (each strand sits R off the middle and is ~R thick),
	// wandering a little sideways and out so it is not a perfect pipe.
	TArray<float> R0, U;
	TArray<FVector> C;
	const TArray<FVector> T0 = Tangents(Line.P);
	float Length = 0.f;
	for (int32 i = 0; i < Num; ++i)
	{
		if (i) Length += FVector::Dist(Line.P[i - 1], Line.P[i]);
		U.Add(Length);
		R0.Add(Style.Radius * Line.Thick[i]);
	}
	for (int32 i = 0; i < Num; ++i)
	{
		U[i] /= FMath::Max(Length, 1.f);
		const float S = U[i] * Length;
		const FVector Lat = Unit(FVector::CrossProduct(T0[i], Line.N[i]), Front);
		const float Wander = 0.35f * R0[i] * FMath::Sin(S * 0.29f + Seed) + 0.2f * R0[i] * FMath::Sin(S * 0.83f + Seed * 2.f);
		const float Lift = Style.Clear + 2.05f * R0[i] + 0.25f * R0[i] * (1.f + FMath::Sin(S * 0.47f + Seed * 3.f));
		C.Add(Line.P[i] + Line.N[i] * Lift + Lat * Wander);
	}
	// Smoothed (waypoint corners, joins between paths), never pulled further into the body than it was.
	for (int32 Pass = 0; Pass < 4; ++Pass)
	{
		TArray<FVector> Next = C;
		for (int32 i = 1; i + 1 < Num; ++i)
		{
			Next[i] = C[i] * 0.5f + (C[i - 1] + C[i + 1]) * 0.25f;
			const FVector Base = Line.P[i] + Line.N[i] * (Style.Clear + 2.05f * R0[i]);
			const float In = FVector::DotProduct(Next[i] - Base, Line.N[i]);
			if (In < 0.f) Next[i] -= Line.N[i] * In;
		}
		C = MoveTemp(Next);
	}
	// Which bone each station rides, and the joint sections where it changes (their stations posed every frame).
	PlanJoints(Line, C);
	StationIds.Init(INDEX_NONE, Num);
	for (int32 i = 0; i < Num; ++i)
	{
		if (Owners[i].Pair != INDEX_NONE) StationIds[i] = Joints->Stations.Add({ C[i], Owners[i].W, Owners[i].Pair });
	}
	TArray<FVector> F1, F2;
	Frames(C, Line.N[0], F1, F2);
	TArray<float> Taper;
	for (int32 i = 0; i < Num; ++i)
	{
		Taper.Add((0.15f + 0.85f * Smooth(0.f, Style.Taper.X, U[i])) * (0.05f + 0.95f * (1.f - Smooth(1.f - Style.Taper.Y, 1.f, U[i]))));
	}
	// Per level: the strands, the core and the winding strand where it draws strands, else one rope tube.
	FRings StrandRings, CoreRings, WindRings, RopeRings;
	for (int32 L = 0; L < NumGraftLevels; ++L)
	{
		const FWWOGraftDetail& D = Detail[L];
		StrandRings.Sides[L] = D.bStrands ? D.StrandSides : 0;
		StrandRings.Step[L] = D.Step;
		CoreRings.Sides[L] = D.bStrands && Style.bCore ? D.CoreSides : 0;
		CoreRings.Step[L] = D.CoreStep;
		WindRings.Sides[L] = D.bStrands ? D.WindSides : 0;
		WindRings.Step[L] = D.Step;
		RopeRings.Sides[L] = D.bStrands ? 0 : D.RopeSides;
		RopeRings.Step[L] = D.Step;
	}
	// The strands, twisting round the middle.
	const float Phase = Random.FRand() * UE_TWO_PI;
	for (int32 k = 0; k < Style.Strands; ++k)
	{
		TArray<FVector> Path;
		TArray<float> Rr;
		for (int32 i = 0; i < Num; ++i)
		{
			const float A = Phase + UE_TWO_PI * k / Style.Strands + UE_TWO_PI * U[i] * Length / Style.Pitch;
			const float Wob = 1.f + 0.08f * FMath::Sin(13.f * U[i] + 2.1f * k + Seed);
			const float Sink = FMath::Lerp(0.6f, 1.f, FMath::Sqrt(Taper[i]));
			Path.Add(C[i] + (F1[i] * FMath::Cos(A) + F2[i] * FMath::Sin(A)) * R0[i] * Wob * Sink);
			Rr.Add(R0[i] * 1.05f * (1.f + 0.07f * FMath::Sin(9.f * U[i] + 1.7f * k)) * Taper[i]);
		}
		Tube(FWWOVineMesh::Bark, Path, Rr, F1, F2, StrandRings, 1.f / (UE_TWO_PI * Style.Radius));
	}
	{
		TArray<float> Rope;
		for (int32 i = 0; i < Num; ++i) Rope.Add(R0[i] * 1.8f * Taper[i]);
		Tube(FWWOVineMesh::Bark, C, Rope, F1, F2, RopeRings, 1.f / (UE_TWO_PI * Style.Radius));
	}
	if (Style.bCore)
	{
		TArray<float> Rc;
		for (int32 i = 0; i < Num; ++i) Rc.Add(R0[i] * 0.62f * (0.2f + 0.8f * FMath::Pow(FMath::Sin(UE_PI * U[i]), 0.3f)));
		Tube(FWWOVineMesh::Core, C, Rc, F1, F2, CoreRings, 0.05f);
	}
	Pieces += Style.Strands + (Style.bCore ? 1 : 0);
	// A thinner rope weaving over this one: on top of it where it crosses, beside it between.
	if (Style.WindRadius > 0.f)
	{
		TArray<FVector> W;
		TArray<float> Rw;
		const float WindPhase = Random.FRand() * UE_TWO_PI;
		const TArray<FVector> TC = Tangents(C);
		for (int32 i = 0; i < Num; ++i)
		{
			const FVector Lat = Unit(FVector::CrossProduct(TC[i], Line.N[i]), Front);
			const float A = WindPhase + UE_TWO_PI * U[i] * Length / Style.WindPitch;
			const float Over = 2.05f * R0[i] + Style.WindRadius * 1.6f;
			const float Ends = Smooth(0.f, 0.06f, U[i]) * (1.f - Smooth(0.94f, 1.f, U[i]));
			W.Add(C[i] + (Lat * FMath::Sin(A) * (Over + 0.3f) + Line.N[i] * Over * FMath::Square(FMath::Cos(A)) * 0.85f) * Ends);
			Rw.Add(Style.WindRadius * (0.25f + 0.75f * Ends) * FMath::Min(Taper[i] * 2.5f, 1.f));
		}
		TArray<FVector> G1, G2;
		Frames(W, Line.N[0], G1, G2);
		for (int32 k = 0; k < 2; ++k)
		{
			TArray<FVector> Path;
			TArray<float> Rr;
			for (int32 i = 0; i < Num; ++i)
			{
				const float A = Phase + UE_PI * k + UE_TWO_PI * U[i] * Length / (Style.Pitch * 0.7f);
				Path.Add(W[i] + (G1[i] * FMath::Cos(A) + G2[i] * FMath::Sin(A)) * Rw[i] * 0.8f);
				Rr.Add(Rw[i]);
			}
			Tube(FWWOVineMesh::Bark, Path, Rr, G1, G2, WindRings, 1.f / (UE_TWO_PI * Style.Radius));
		}
		++Pieces;
		if (Style.ThornEvery > 0.f) // thorns sticking out of it, hooked a little along it
		{
			const int32 Every = FMath::Max(2, FMath::RoundToInt(Style.ThornEvery / FMath::Max(Length / (Num - 1), 0.1f)));
			for (int32 i = 3; i + 3 < Num; i += Every)
			{
				const FVector Out = Unit(W[i] - C[i] + Line.N[i] * 0.5f);
				const FVector Along = Unit(W[FMath::Min(i + 1, Num - 1)] - W[i - 1]);
				Cone(BoneAt(i), W[i] - Out * Rw[i] * 0.3f, Unit(Out + Along * 0.35f), Style.ThornLength, Rw[i] * 0.9f);
			}
		}
	}
	// Crystal pods (where asked, turned toward Prefer within a few cm), then the leaves round them.
	TArray<TPair<FVector, float>> Pods;
	for (const float At : Style.PodsAt)
	{
		int32 Best = FMath::Clamp(FMath::RoundToInt(At * (Num - 1)), 0, Num - 1);
		if (!Style.PodPrefer.IsZero())
		{
			float BestScore = -1e9f;
			const int32 Window = FMath::RoundToInt(0.06f * Num);
			for (int32 i = FMath::Max(0, Best - Window); i <= FMath::Min(Num - 1, Best + Window); ++i)
			{
				const float Score = FVector::DotProduct(Line.N[i], Style.PodPrefer) - 3.f * FMath::Abs(U[i] - At);
				if (Score > BestScore) { BestScore = Score; Best = i; }
			}
		}
		const FVector Out = Line.N[Best];
		Crystal(BoneAt(Best), C[Best] + Out * R0[Best], Unit(Out + Front * 0.15f), Style.PodSize * Line.Thick[Best]);
		Pods.Add({ C[Best], Style.PodSize * 30.f * 0.4f }); // leaves keep clear of it (as the fitted bulbs)
	}
	if (Style.LeafEvery > 0.f)
	{
		// Every LeafEvery cm a leaf or two, each from its own place round the vine (anywhere on the half turned
		// away from the body) a step up or down it, hanging nearly straight down: a little more out from one on
		// the upper side, which has to clear the vine (the fitted leaves_along, leaves=original).
		const TArray<FVector> TC = Tangents(C);
		const int32 Reach = FMath::Max(1, FMath::RoundToInt(Style.LeafEvery * 0.5f / FMath::Max(Length / (Num - 1), 0.1f)));
		float Next = Style.LeafEvery * Random.FRandRange(0.3f, 1.f);
		float Walked = 0.f;
		for (int32 i = 0; i < Num; ++i)
		{
			if (i) Walked += FVector::Dist(C[i - 1], C[i]);
			if (Walked < Next) continue;
			Next = Walked + Style.LeafEvery * Random.FRandRange(0.75f, 1.25f);
			if (U[i] < Style.LeafSkip.X || U[i] > 1.f - Style.LeafSkip.Y || Line.Leafy[i] < 0.5f) continue;
			if (Pods.ContainsByPredicate([&](const TPair<FVector, float>& P) { return FVector::Dist(P.Key, C[i]) < P.Value; })) continue;
			const int32 Count = Random.RandRange(Style.LeafCount.X, Style.LeafCount.Y);
			for (int32 m = 0; m < Count; ++m)
			{
				const int32 j = FMath::Clamp(i + Random.RandRange(-Reach, Reach), 1, Num - 2);
				const FVector O = Line.N[j].RotateAngleAxisRad(Random.FRandRange(-1.5f, 1.5f), TC[j]);
				const float Lift = FMath::Max(0.f, float(FVector::DotProduct(O, Up)));
				const float B = FMath::DegreesToRadians(Random.FRandRange(Style.HangDeg.X, Style.HangDeg.Y) + 14.f * Lift);
				const FVector Dir = Unit(-Up * FMath::Cos(B) + O * FMath::Sin(B) + FVector::CrossProduct(TC[j], O) * Gauss(Random, 0.55f) * 0.22f);
				const float Size = Random.FRandRange(Style.LeafSize.X, Style.LeafSize.Y) * Random.FRandRange(0.75f, 1.25f) * Line.LeafScale[j];
				Leaf(BoneAt(j), C[j] + O * (R0[j] * 0.9f), Dir, O + Up * 0.2f, Size, Random.RandRange(0, 4));
			}
		}
	}
	// Tendrils: spread along it, and off its end.
	for (int32 k = 0; k < Style.Tendrils; ++k)
	{
		const int32 i = FMath::Clamp(FMath::RoundToInt((0.15f + 0.75f * (k + Random.FRandRange(0.2f, 0.8f)) / Style.Tendrils) * (Num - 1)), 1, Num - 2);
		Tendril(BoneAt(i), C[i] + Line.N[i] * R0[i], Line.N[i], Random.FRandRange(3.5f, 5.5f));
	}
	if (Style.bTendrilEnd)
	{
		Tendril(BoneAt(Num - 2), C[Num - 2], Unit(C[Num - 1] - C[FMath::Max(Num - 4, 0)]) + Line.N[Num - 1] * 0.5f, Random.FRandRange(4.f, 5.5f));
	}
}

void FWWOVineGraft::Leaf(FName Bone, const FVector& At, const FVector& Dir, const FVector& Face, float Length, int32 Kind)
{
	// One of the fitted grafts' whole leaves (SM_VV_Leaf, Kind 0-4: leaves 01-04 and 06), Length cm from its
	// stalk to its tip, pointing along Dir with its top toward Face. It leaves the vine on a short green shoot,
	// nearly as thick as a strand where it joins, that turns into its thin stalk (make_voltvine_grafts leaf()).
	// Each level draws it from that level's LOD of the leaf mesh, sized by the full one's length.
	const FWWOLeafShape* Full = LeafShape(Kind, 0);
	if (!Full) return;
	// Which levels keep it: a fixed draw per leaf (not from Random: every level sees the same placements), under each
	// level's share, so a farther level keeps a subset of a nearer one's.
	const uint32 Hash = (++LeafSerial) * 2654435761u;
	const float Draw = float((Hash >> 8) % 10000u) / 10000.f;
	bool bKeep[NumGraftLevels];
	++Leaves;
	for (int32 L = 0; L < NumGraftLevels; ++L)
	{
		bKeep[L] = Draw < Detail[L].LeafKeep;
		LeavesKept[L] += bKeep[L] ? 1 : 0;
	}
	const FVector D = Unit(Dir), Fo = Unit(Face);
	const FVector Root = At - Fo * 0.6f - D * 0.3f, Knee = At + Fo * 1.6f + D * 2.2f, Mid = At + Fo * 1.4f + D * 0.6f;
	TArray<FVector> Twig;
	TArray<float> R;
	for (int32 k = 0; k < 5; ++k)
	{
		const float T = k / 4.f;
		Twig.Add(Root * FMath::Square(1.f - T) + Mid * 2.f * (1.f - T) * T + Knee * T * T);
		R.Add(0.36f + 0.74f * FMath::Pow(1.f - T, 1.6f));
	}
	FRings TwigRings, PetioleRings;
	for (int32 L = 0; L < NumGraftLevels; ++L)
	{
		TwigRings.Sides[L] = bKeep[L] ? Detail[L].TwigSides : 0;
		TwigRings.Step[L] = Detail[L].TwigStep;
		PetioleRings.Sides[L] = bKeep[L] ? Detail[L].PetioleSides : 0;
		PetioleRings.Step[L] = Detail[L].PetioleStep;
	}
	Strip(Bone, FWWOVineMesh::Stem, Twig, R, TwigRings);
	const FVector End = Knee + D * Length * 0.15f * 0.6f + Fo * Length * 0.15f * 0.12f;
	{
		const FVector A = Knee - D * 0.3f, Out = Unit(D + Fo * 0.5f);
		const float Ln = FVector::Dist(A, End);
		const FVector P1 = A + Out * Ln * 0.45f, P2 = End - D * Ln * 0.45f;
		TArray<FVector> Pet;
		TArray<float> Rp;
		for (int32 k = 0; k < 7; ++k)
		{
			const float T = k / 6.f, S = 1.f - T;
			Pet.Add(A * S * S * S + P1 * 3.f * S * S * T + P2 * 3.f * S * T * T + End * T * T * T);
			Rp.Add(0.21f * (1.f - 0.35f * T) * (1.f + 0.9f * (1.f - Smooth(0.f, 0.2f, T))));
		}
		Strip(Bone, FWWOVineMesh::Stem, Pet, Rp, PetioleRings);
	}
	// The leaf itself: its stalk's base at End, along D, top toward Face turned a little about D.
	const FVector Top = Fo.RotateAngleAxisRad(Gauss(Random, 0.2f), D);
	const FMatrix M = FRotationMatrix::MakeFromXZ(D, Top);
	const float Scale = Length / Full->Length;
	for (int32 L = 0; L < NumGraftLevels; ++L)
	{
		const FWWOLeafShape* Shape = bKeep[L] ? LeafShape(Kind, Detail[L].LeafLod) : nullptr;
		if (!Shape) continue;
		for (int32 s = 0; s < 2; ++s)
		{
			if (Shape->Tris[s].Num() == 0) continue;
			FWWOVineMesh::FSection& Sec = MeshOn(Bone, L).S[s == 0 ? FWWOVineMesh::Leaf : FWWOVineMesh::Stem];
			const int32 Base = Sec.V.Num();
			for (int32 v = 0; v < Shape->V.Num(); ++v)
			{
				Sec.V.Add(End + M.TransformVector(Shape->V[v] * Scale));
				Sec.N.Add(M.TransformVector(Shape->N[v]));
				Sec.T.Add(FProcMeshTangent(M.TransformVector(Shape->T[v]), false));
				Sec.UV.Add(Shape->UV[v]);
			}
			for (const int32 Index : Shape->Tris[s]) Sec.Tris.Add(Base + Index);
		}
	}
	++Pieces;
}

void FWWOVineGraft::LeafFan(FName Bone, const FVector& At, const FVector& Out, const FVector& Spread, int32 Count, FVector2D Size, float Down)
{
	// Leaves falling from one spot (the fitted Static Charge's over each shoulder), fanned along Spread.
	for (int32 k = 0; k < Count; ++k)
	{
		const float Sp = (k - (Count - 1) * 0.5f) * 0.32f;
		const FVector Dir = Unit(Out * 0.42f - Up * Down + Spread * Sp * 0.7f);
		Leaf(Bone, At + Spread * Sp * 5.f + Out, Dir, Out + Up * 0.3f, Random.FRandRange(Size.X, Size.Y), Random.RandRange(0, 4));
	}
}

void FWWOVineGraft::Tendril(FName Bone, const FVector& At, const FVector& InOut, float Length)
{
	// A thin shoot off the vine, arcing out and curling into a tightening spiral.
	const FVector Out = Unit(Unit(InOut) + Gauss3(Random, 0.25f));
	const FVector U = FMath::Abs(FVector::DotProduct(Out, Up)) < 0.95f ? Unit(Up - Out * FVector::DotProduct(Out, Up)) : Front;
	const FVector Side = Unit(FVector::CrossProduct(Out, U));
	const float Coils = Random.FRandRange(1.2f, 1.8f);
	constexpr int32 Count = 20;
	TArray<FVector> Path;
	TArray<float> R;
	for (int32 i = 0; i < Count; ++i)
	{
		const float S = float(i) / (Count - 1);
		const float Reach = Length * FMath::Min(S / 0.55f, 1.f);
		FVector P = At + Out * Reach * 0.8f + U * Length * 0.3f * FMath::Sin(FMath::Min(S / 0.55f, 1.f) * 1.4f);
		if (S > 0.45f)
		{
			const float Q = (S - 0.45f) / 0.55f, A = UE_TWO_PI * Coils * Q, Rad = Length * 0.18f * (1.f - 0.7f * Q);
			P += (Out * FMath::Sin(A) - U * (1.f - FMath::Cos(A)) + Side * 0.4f * Q) * Rad;
		}
		Path.Add(P);
		R.Add(0.22f * (1.f - 0.8f * S) + 0.03f);
	}
	Strip(Bone, FWWOVineMesh::Stem, Path, R, SmallRings(5));
	++Pieces;
}

void FWWOVineGraft::Cone(FName Bone, const FVector& At, const FVector& Dir, float Length, float Radius)
{
	TArray<FVector> Path;
	TArray<float> R;
	for (int32 i = 0; i < 4; ++i)
	{
		const float S = i / 3.f;
		Path.Add(At + Dir * Length * S);
		R.Add(Radius * (1.f - S) + 0.02f);
	}
	FRings Rings = SmallRings(5);
	for (int32& Step : Rings.Step) Step = 1; // (only 4 rings: all of them)
	Strip(Bone, FWWOVineMesh::Bark, Path, R, Rings);
	++Pieces;
}

void FWWOVineGraft::Crystal(FName Bone, const FVector& At, const FVector& InOut, float Scale)
{
	// The fitted bulb: an almond-shaped crystal (pointed both ends, flattened front to back, flat facets, glowing
	// from inside) lying down along the surface, pushed out a little, two thin vines curling round it, and an arc
	// of charge off it. Each level with its own rings round it.
	const FVector Out = Unit(InOut);
	const float L = Scale * 30.f;
	const FVector Axis = Unit(-Up + Out * 0.15f + FVector::CrossProduct(Out, Up) * Gauss(Random, 0.25f), -Front);
	const FVector Center = At + Out * (L * 0.18f + 0.4f) + Axis * L * 0.28f;
	const FVector Face = Unit(Out - Axis * FVector::DotProduct(Out, Axis));
	const FVector Side = Unit(FVector::CrossProduct(Axis, Face));
	const float Rm = L * 0.31f;
	for (int32 Level = 0; Level < NumGraftLevels; ++Level)
	{
		const int32 Rings = FMath::Max(Detail[Level].CrystalRings, 3), Around = FMath::Max(Detail[Level].CrystalAround, 3);
		// The rings (and the two tips), then flat-shaded triangles.
		TArray<FVector> P;
		TArray<FVector2D> UV;
		P.Add(Center - Axis * L * 0.5f);
		UV.Add(FVector2D(0.5f, 0.f));
		for (int32 i = 1; i < Rings; ++i)
		{
			const float T = float(i) / Rings, Rr = Rm * FMath::Pow(FMath::Sin(UE_PI * T), 0.8f);
			for (int32 j = 0; j <= Around; ++j)
			{
				const float A = UE_TWO_PI * j / Around + (i % 2 ? UE_PI / Around : 0.f);
				const float X = FMath::Cos(A), Y = FMath::Sin(A) * 0.68f;
				P.Add(Center + Axis * L * (T - 0.5f) + (Side * X + Face * Y) * Rr);
				UV.Add(FVector2D(0.5f + 0.5f * X * Rr / Rm * 0.75f, T));
			}
		}
		P.Add(Center + Axis * L * 0.5f);
		UV.Add(FVector2D(0.5f, 1.f));
		const int32 S = Around + 1, Top = P.Num() - 1;
		TArray<FIntVector> Faces;
		for (int32 j = 0; j < Around; ++j) Faces.Add(FIntVector(0, 1 + j + 1, 1 + j));
		for (int32 i = 0; i < Rings - 2; ++i)
		{
			const int32 A0 = 1 + i * S, B0 = A0 + S;
			for (int32 j = 0; j < Around; ++j)
			{
				Faces.Add(FIntVector(A0 + j, A0 + j + 1, B0 + j + 1));
				Faces.Add(FIntVector(A0 + j, B0 + j + 1, B0 + j));
			}
		}
		const int32 Last = 1 + (Rings - 2) * S;
		for (int32 j = 0; j < Around; ++j) Faces.Add(FIntVector(Last + j, Last + j + 1, Top));
		FWWOVineMesh::FSection& Sec = MeshOn(Bone, Level).S[FWWOVineMesh::Crystal];
		for (const FIntVector& F : Faces)
		{
			FVector Nf = FVector::CrossProduct(P[F.Y] - P[F.X], P[F.Z] - P[F.X]).GetSafeNormal();
			const FVector Mid = (P[F.X] + P[F.Y] + P[F.Z]) / 3.f;
			const bool bFlip = FVector::DotProduct(Nf, Mid - Center) < 0.f; // whichever way round, facing out
			if (bFlip) Nf = -Nf;
			const int32 Base = Sec.V.Num();
			for (const int32 k : { F.X, F.Y, F.Z })
			{
				Sec.V.Add(P[k]);
				Sec.N.Add(Nf);
				Sec.UV.Add(FVector2D(FMath::Lerp(CrystalIsland.U0, CrystalIsland.U1, UV[k].X), FMath::Lerp(CrystalIsland.V1, CrystalIsland.V0, UV[k].Y)));
				Sec.T.Add(FProcMeshTangent(Side, false));
			}
			if (bFlip) Tri(Sec.Tris, Base, Base + 2, Base + 1);
			else Tri(Sec.Tris, Base, Base + 1, Base + 2);
		}
	}
	// The cradle: two thin vines from the stem round its sides.
	for (const float Sgn : { -1.f, 1.f })
	{
		TArray<FVector> Path;
		TArray<float> R;
		Path.Add(At + Side * Sgn * 0.3f);
		R.Add(0.22f * FMath::Sqrt(Scale / 0.4f));
		for (int32 q = 1; q < 16; ++q)
		{
			const float Q = q / 15.f, A = UE_PI * Q;
			Path.Add(Center + Axis * L * (Q - 0.5f) * 1.05f + Side * Sgn * L * 0.3f * FMath::Sin(A) * 1.1f + Out * L * 0.12f * FMath::Sin(A * 2.f) - Out * 0.2f);
			R.Add(0.22f * FMath::Sqrt(Scale / 0.4f) * (1.f - 0.6f * Q));
		}
		Strip(Bone, FWWOVineMesh::Bark, Path, R, SmallRings(5));
	}
	if (Scale > 0.2f && Random.FRand() < 0.7f) // an arc of charge
	{
		const FVector A0 = Center + Axis * L * 0.35f + Side * L * 0.2f;
		const FVector A1 = A0 + Side * L * 0.5f - Axis * L * 0.25f + Out;
		TArray<FVector> Path;
		TArray<float> R;
		for (int32 q = 0; q <= 7; ++q)
		{
			Path.Add(A0 + (A1 - A0) * (q / 7.f) + ((q > 0 && q < 7) ? Gauss3(Random, L * 0.05f) : FVector::ZeroVector));
			R.Add(0.09f);
		}
		FRings Rings = SmallRings(4);
		for (int32 Lv = 1; Lv < NumGraftLevels; ++Lv) Rings.Sides[Lv] = 0; // (a hair-thin arc: only up close)
		Strip(Bone, FWWOVineMesh::Core, Path, R, Rings);
	}
	++Pieces;
}

// --- VeilSpore: kit pieces and mycelium ----------------------------------------------------------

void FWWOVineGraft::KitPiece(FName Bone, const FWWOLeafShape* const (&Shapes)[NumGraftLevels], const FWWOLeafShape* const (&SmallShapes)[NumGraftLevels],
	const FTransform& Xf)
{
	if (!Shapes[0]) return;
	const float Across = float(Shapes[0]->Bounds.GetSize().GetMax() * Xf.GetScale3D().GetMax());
	for (int32 L = 0; L < NumGraftLevels; ++L)
	{
		if (Across < Detail[L].KitMin) continue; // too small to see that far
		const FWWOLeafShape* Shape = Across < Detail[L].KitSmall && SmallShapes[L] ? SmallShapes[L] : Shapes[L] ? Shapes[L] : Shapes[0];
		FWWOVineMesh::FSection& Sec = MeshOn(Bone, L).S[FWWOVineMesh::Fungus];
		const int32 Base = Sec.V.Num();
		for (int32 v = 0; v < Shape->V.Num(); ++v)
		{
			Sec.V.Add(Xf.TransformPosition(Shape->V[v]));
			Sec.N.Add(Xf.TransformVectorNoScale(Shape->N[v]));
			Sec.T.Add(FProcMeshTangent(Xf.TransformVectorNoScale(Shape->T[v]), false));
			Sec.UV.Add(Shape->UV[v]);
		}
		for (const TArray<int32>& Tris : Shape->Tris) for (const int32 Index : Tris) Sec.Tris.Add(Base + Index);
		KitTris[L] += (Shape->Tris[0].Num() + Shape->Tris[1].Num()) / 3;
	}
	++Pieces;
}

int32 FWWOVineGraft::FungusTris(int32 L) const
{
	int32 N = Joints->S[L][FWWOVineMesh::Fungus].Tris.Num() / 3;
	for (const TPair<FName, FWWOVineMeshLevels>& It : Rigid) N += It.Value.L[L].S[FWWOVineMesh::Fungus].Tris.Num() / 3;
	return N;
}

void FWWOVineGraft::Net(const FWWOVineLine& InLine, const FWWONetStyle& Style, const FWWOShellSurface& Surface, float Sign,
	const TArray<FVector4>& Glow, int32 Seed)
{
	// Knots in rows across the line (staggered, jittered), each walked over the surface from its row's point on
	// the line; the band narrows to nothing over its ends (Taper). Its points ride the line's bones, posed between
	// two of them where the line passes a joint (as a vine's).
	const FWWOVineLine Line = Resample(InLine, 1.f);
	const int32 Num = Line.Num();
	if (Num < 3) return;
	FRandomStream R(Seed);
	PlanJoints(Line, Line.P);
	const TArray<FStationOwner> LineOwners = Owners;
	float Len = 0.f;
	for (int32 i = 1; i < Num; ++i) Len += FVector::Dist(Line.P[i - 1], Line.P[i]);
	const TArray<FVector> T = Tangents(Line.P);
	auto IndexAt = [&](float S) { return FMath::Clamp(FMath::RoundToInt(S / FMath::Max(Len, 1.f) * (Num - 1)), 0, Num - 1); };
	TArray<FNetKnot> Knots;
	const float Row = Style.Cell * 0.87f;
	int32 k = 0;
	for (float S0 = Row * 0.35f; S0 < Len - Row * 0.15f; S0 += Row, ++k)
	{
		const float U = S0 / Len;
		const float Narrow = FMath::Max(Smooth(0.f, Style.Taper.X, U) * (1.f - Smooth(1.f - Style.Taper.Y, 1.f, U)), 0.1f);
		const float WL = Style.Width.X * Narrow, WR = Style.Width.Y * Narrow;
		for (int32 j = -FMath::CeilToInt(WL / Style.Cell) - 1; j <= FMath::CeilToInt(WR / Style.Cell) + 1; ++j)
		{
			const float S = FMath::Clamp(S0 + R.FRandRange(-0.3f, 0.3f) * Style.Cell, 0.f, Len);
			const float D = (j + 0.5f * (k % 2)) * Style.Cell + R.FRandRange(-0.32f, 0.32f) * Style.Cell;
			if (D < -WL || D > WR) continue;
			if (FMath::Abs(D) > Style.Cell * 0.45f && R.FRand() < Style.Holes) continue; // (the middle stays whole)
			const int32 i = IndexAt(S);
			FNetKnot K;
			K.P = Line.P[i];
			K.N = Line.N[i];
			K.Out = Unit(FVector::CrossProduct(T[i], Line.N[i]), Front) * Sign * (D < 0.f ? -1.f : 1.f);
			if (FMath::Abs(D) > 0.05f) Surface.Walk(K.P, K.N, K.Out, FMath::Abs(D));
			K.Out = Unit(K.Out - K.N * FVector::DotProduct(K.Out, K.N), K.Out);
			K.Q = FVector2D(S, D);
			K.Edge = FMath::Clamp(D < 0.f ? -D / FMath::Max(WL, 0.1f) : D / FMath::Max(WR, 0.1f), 0.f, 1.f);
			K.Along = S;
			K.Own = LineOwners[i];
			Knots.Add(K);
		}
	}
	NetBuild(Knots, Style, Surface, Glow, R, [&](float Along) { return LineOwners[IndexAt(Along)]; });
}

void FWWOVineGraft::NetPatch(FName Bone, const FVector& At, const FVector& N0, const FWWONetStyle& Style, const FWWOShellSurface& Surface,
	const TArray<FVector4>& Glow, int32 Seed)
{
	// Knots over a disc round the spot (a ragged rim), each walked out from it over the surface; all on its bone.
	FRandomStream R(Seed);
	const FVector N = Unit(N0);
	const FVector E1 = Unit(FVector::CrossProduct(N, FMath::Abs(N.Z) < 0.9f ? Up : Front)), E2 = FVector::CrossProduct(N, E1);
	const float Radius = Style.Width.X;
	const float Row = Style.Cell * 0.87f;
	const float Lobes = R.FRandRange(0.f, UE_TWO_PI);
	TArray<FNetKnot> Knots;
	int32 k = 0;
	for (float Y = -Radius; Y <= Radius; Y += Row, ++k)
	{
		for (float X = -Radius - Style.Cell; X <= Radius + Style.Cell; X += Style.Cell)
		{
			const FVector2D Q(X + 0.5f * (k % 2) * Style.Cell + R.FRandRange(-0.32f, 0.32f) * Style.Cell, Y + R.FRandRange(-0.3f, 0.3f) * Style.Cell);
			const float Dist = Q.Size();
			const float Rim = Radius * (0.82f + 0.2f * FMath::Sin(FMath::Atan2(Q.Y, Q.X) * 3.f + Lobes));
			if (Dist > Rim || (Dist > Style.Cell * 0.6f && R.FRand() < Style.Holes)) continue;
			FNetKnot K;
			K.P = At;
			K.N = N;
			K.Out = Dist > 0.01f ? Unit(E1 * Q.X + E2 * Q.Y) : E1;
			if (Dist > 0.05f) Surface.Walk(K.P, K.N, K.Out, Dist);
			K.Out = Unit(K.Out - K.N * FVector::DotProduct(K.Out, K.N), K.Out);
			K.Q = Q;
			K.Edge = FMath::Clamp(Dist / Rim, 0.f, 1.f);
			K.Own = { Bone, INDEX_NONE, 0.f };
			Knots.Add(K);
		}
	}
	NetBuild(Knots, Style, Surface, Glow, R, [Bone](float) { return FStationOwner{ Bone, INDEX_NONE, 0.f }; });
}

void FWWOVineGraft::NetBuild(const TArray<FNetKnot>& AllKnots, const FWWONetStyle& Style, const FWWOShellSurface& Surface,
	const TArray<FVector4>& Glow, FRandomStream& R, const TFunctionRef<FStationOwner(float Along)> OwnerAt)
{
	// Knots inside an obstacle (Blocked) are left out: the net grows round it.
	TArray<FNetKnot> Kept;
	if (Blocked) for (const FNetKnot& K : AllKnots) { if (Blocked(K.P)) ++KnotsBlocked; else Kept.Add(K); }
	const TArray<FNetKnot>& Knots = Blocked ? Kept : AllKnots;
	const int32 Num = Knots.Num();
	if (Num < 2) return;
	int32 Before[NumGraftLevels];
	for (int32 L = 0; L < NumGraftLevels; ++L) Before[L] = FungusTris(L);
	// The strands: a beta-skeleton over the knots in the net's own plane (an edge stays when no other knot is in
	// the lune between its ends; 1.45: between the Gabriel graph and the relative neighbourhood graph, the
	// fitted lattice's 1.55), so the net is connected and its holes come in many sizes.
	constexpr float Beta = 1.45f;
	const float Reach = Style.Cell * 2.4f;
	TArray<TArray<int32>> Near;
	Near.SetNum(Num);
	for (int32 a = 0; a < Num; ++a)
	{
		for (int32 b = a + 1; b < Num; ++b)
		{
			if (FVector2D::DistSquared(Knots[a].Q, Knots[b].Q) < Reach * Reach) { Near[a].Add(b); Near[b].Add(a); }
		}
	}
	TArray<FIntPoint> Edges;
	TArray<int32> Degree;
	Degree.Init(0, Num);
	for (int32 a = 0; a < Num; ++a)
	{
		for (const int32 b : Near[a])
		{
			if (b < a) continue;
			const FVector2D QA = Knots[a].Q, QB = Knots[b].Q;
			const float Rr = Beta * FVector2D::Distance(QA, QB) * 0.5f;
			const FVector2D C1 = QA + (QB - QA) * (Beta * 0.5f), C2 = QB + (QA - QB) * (Beta * 0.5f);
			bool bBlocked = false;
			for (const int32 c : Near[a])
			{
				if (c != b && FVector2D::Distance(Knots[c].Q, C1) < Rr && FVector2D::Distance(Knots[c].Q, C2) < Rr) { bBlocked = true; break; }
			}
			if (bBlocked) continue;
			Edges.Add(FIntPoint(a, b));
			++Degree[a];
			++Degree[b];
		}
	}
	// Each knot's thickness: thick in the middle, thin at the edge, a little uneven.
	TArray<float> KnotR;
	for (const FNetKnot& K : Knots) KnotR.Add(FMath::Lerp(Style.Strand.X, Style.Strand.Y, FMath::Pow(K.Edge, 0.8f)) * R.FRandRange(0.82f, 1.15f));
	// Its look per point: the 'net' island, U by thickness, V by how near a cap it is.
	auto UVAt = [&](const FVector& P, float Rad)
	{
		float G = 0.f;
		for (const FVector4& Gp : Glow) G = FMath::Max(G, 1.f - Smooth(float(Gp.W) * 0.3f, float(Gp.W), float(FVector::Dist(P, FVector(Gp)))));
		G = FMath::Clamp(G * Style.Glow, 0.f, 1.f);
		const float Th = FMath::Clamp((Rad - 0.06f) / FMath::Max(Style.Strand.X * 1.15f - 0.06f, 0.05f), 0.f, 1.f);
		return FVector2D(FMath::Lerp(NetIsland.U0, NetIsland.U1, 0.05f + 0.9f * Th), FMath::Lerp(NetIsland.V1, NetIsland.V0, G));
	};
	// One strand through points on the surface: lifted off it by its own radius, rigid on bones or posed between two.
	auto Strand = [&](const TArray<FVector>& Pts, const TArray<FVector>& Ns, const TArray<float>& Rad, const TArray<FStationOwner>& Own, bool bRoot)
	{
		const int32 M = Pts.Num();
		if (M < 2) return;
		TArray<FVector> C;
		TArray<FVector2D> UV;
		float Widest = 0.f;
		for (int32 i = 0; i < M; ++i)
		{
			C.Add(Pts[i] + Ns[i] * (Style.Clear + Rad[i]));
			UV.Add(UVAt(Pts[i], Rad[i]));
			Widest = FMath::Max(Widest, Rad[i]);
		}
		TArray<FVector> F1, F2;
		Frames(C, Ns[0], F1, F2);
		Owners = Own;
		StationIds.Init(INDEX_NONE, M);
		for (int32 i = 0; i < M; ++i) if (Own[i].Pair != INDEX_NONE) StationIds[i] = Joints->Stations.Add({ C[i], Own[i].W, Own[i].Pair });
		// Per level: thick strands rounder than thin ones; the farthest leaves the thinnest out.
		FRings Rings;
		for (int32 L = 0; L < NumGraftLevels; ++L)
		{
			const FWWOGraftDetail& D = Detail[L];
			Rings.Sides[L] = Widest < D.NetMinRadius || (bRoot && !D.bSmall) ? 0 : Widest > 0.3f ? D.NetSides : D.NetThinSides;
			Rings.Step[L] = D.NetStep;
		}
		Tube(FWWOVineMesh::Fungus, C, Rad, F1, F2, Rings, 0.f, &UV);
	};
	for (const FIntPoint& E : Edges)
	{
		const FNetKnot& A = Knots[E.X];
		const FNetKnot& B = Knots[E.Y];
		const float Len = FVector::Dist(A.P, B.P);
		const int32 Steps = FMath::Max(2, FMath::CeilToInt(Len / 1.f));
		const float Sway = R.FRandRange(-0.15f, 0.15f) * Len; // strands bow a little, not ruled lines
		const FVector Side = Unit(FVector::CrossProduct(B.P - A.P, A.N + B.N), Front);
		TArray<FVector> Pts, Ns;
		TArray<float> Rad;
		TArray<FStationOwner> Own;
		for (int32 s = 0; s <= Steps; ++s)
		{
			const float T = float(s) / Steps;
			FVector P = FMath::Lerp(A.P, B.P, T) + Side * Sway * 4.f * T * (1.f - T);
			FVector N = Unit(FMath::Lerp(A.N, B.N, T), A.N);
			if (s > 0 && s < Steps) Surface.Project(P, N);
			Pts.Add(P);
			Ns.Add(N);
			Rad.Add(FMath::Lerp(KnotR[E.X], KnotR[E.Y], T) * (1.f - 0.28f * FMath::Sin(UE_PI * T))); // full at the knots: fused joins
			Own.Add(OwnerAt(FMath::Lerp(A.Along, B.Along, T)));
		}
		Strand(Pts, Ns, Rad, Own, false);
	}
	// A knob where three or more strands meet (the fitted lattice's fused joins): a flattened ball.
	for (int32 i = 0; i < Num; ++i)
	{
		if (Degree[i] < 3) continue;
		const FNetKnot& K = Knots[i];
		const float Rb = KnotR[i] * 1.45f;
		const FVector C = K.P + K.N * (Style.Clear + Rb * 0.55f);
		const FVector E1 = Unit(FVector::CrossProduct(K.N, FMath::Abs(K.N.Z) < 0.9f ? Up : Front)), E2 = FVector::CrossProduct(K.N, E1);
		const FVector2D UV = UVAt(K.P, Rb);
		const bool bJoint = K.Own.Pair != INDEX_NONE;
		const int32 Station = bJoint ? Joints->Stations.Add({ C, K.Own.W, K.Own.Pair }) : INDEX_NONE;
		for (int32 L = 0; L < NumGraftLevels; ++L)
		{
			const int32 Around = Detail[L].KnobSides, Rings = Detail[L].KnobRings;
			if (Around < 3 || Rings < 2) continue;
			FWWOVineJoints::FSection& JS = Joints->S[L][FWWOVineMesh::Fungus];
			FWWOVineMesh::FSection& RS = MeshOn(K.Own.Bone, L).S[FWWOVineMesh::Fungus];
			TArray<FVector>& V = bJoint ? JS.RefV : RS.V;
			TArray<FVector>& Nm = bJoint ? JS.RefN : RS.N;
			TArray<FVector2D>& UVs = bJoint ? JS.UV : RS.UV;
			TArray<FProcMeshTangent>& Tg = bJoint ? JS.RefT : RS.T;
			TArray<int32>& Tris = bJoint ? JS.Tris : RS.Tris;
			const int32 Base = V.Num();
			for (int32 r = 0; r <= Rings; ++r)
			{
				const float Phi = UE_PI * r / Rings - UE_HALF_PI;
				for (int32 j = 0; j <= Around; ++j)
				{
					const float A = UE_TWO_PI * j / Around;
					const FVector Dir = E1 * FMath::Cos(A) + E2 * FMath::Sin(A);
					V.Add(C + Dir * Rb * FMath::Cos(Phi) + K.N * Rb * 0.6f * FMath::Sin(Phi));
					Nm.Add(Unit(Dir * FMath::Cos(Phi) + K.N * FMath::Sin(Phi) / 0.6f, K.N));
					UVs.Add(UV);
					Tg.Add(FProcMeshTangent(-E1 * FMath::Sin(A) + E2 * FMath::Cos(A), false));
					if (bJoint) JS.Station.Add(Station);
				}
			}
			for (int32 r = 0; r < Rings; ++r)
			{
				for (int32 j = 0; j < Around; ++j)
				{
					const int32 A = Base + r * (Around + 1) + j, B = A + 1, D = A + Around + 1, Cc = D + 1;
					Tri(Tris, A, B, D);
					Tri(Tris, B, Cc, D);
				}
			}
		}
	}
	// Fine roots off the edge: thin runners wandering on outward over the surface, thinning to nothing.
	TArray<int32> Rim;
	for (int32 i = 0; i < Num; ++i) if (Knots[i].Edge > 0.6f && Degree[i] <= 3) Rim.Add(i);
	for (int32 n = 0; n < Style.Roots && Rim.Num() > 0; ++n)
	{
		const int32 Pick = R.RandRange(0, Rim.Num() - 1);
		const FNetKnot& K = Knots[Rim[Pick]];
		const float R0 = KnotR[Rim[Pick]] * 0.85f;
		Rim.RemoveAtSwap(Pick);
		FVector P = K.P, N = K.N, Dir = K.Out;
		const float Length = R.FRandRange(3.f, 7.f);
		constexpr int32 Steps = 8;
		TArray<FVector> Pts = { P }, Ns = { N };
		TArray<float> Rad = { R0 };
		TArray<FStationOwner> Own = { K.Own };
		for (int32 s = 1; s <= Steps; ++s)
		{
			Dir = Dir.RotateAngleAxisRad(R.FRandRange(-0.45f, 0.45f), N);
			Surface.Walk(P, N, Dir, Length / Steps);
			Pts.Add(P);
			Ns.Add(N);
			Rad.Add(FMath::Lerp(R0, 0.05f, float(s) / Steps));
			Own.Add(K.Own);
		}
		Strand(Pts, Ns, Rad, Own, true);
	}
	for (int32 L = 0; L < NumGraftLevels; ++L) NetTris[L] += FungusTris(L) - Before[L];
	++Pieces;
}
