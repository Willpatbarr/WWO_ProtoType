// The body's graft anchors (the worn shell parts' anchors and paths, Characters/WWOShellAnchors.h) and the
// grafts placed from them: a flora's pieces from Config/GraftRecipes.json, built on whatever parts are worn
// and placed again whenever a part is swapped: procedural bark plates and seed pods (FWWOGraftBuilder),
// VoltVine's vines along chains of paths with their leaves, tendrils and crystal pods (FWWOVineGraft), and
// VeilSpore's kit pieces (static meshes: shelf fungi, its hood, caps) laid on anchors and along paths, with its
// mycelium grown over the surface between them (FWWOVineGraft::KitPiece, Net). Plates and pods can be seated on the
// worn parts' surface (FWWOShellSurface::Seat), and every piece keeps off the parts' obstacles (an antenna).

#include "Characters/WWOBodyComponent.h"
#include "Characters/WWOShellAnchors.h"
#include "Characters/WWOGraftBuilder.h"
#include "Characters/WWOVineGraft.h"
#include "Systems/WWOConfig.h"
#include "WWOSwitches.h"
#include "WhatWasOurs.h"
#include "WWOTypes.h"
#include "Engine/World.h"
#include "ProceduralMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "HAL/IConsoleManager.h"
#include "UObject/UObjectIterator.h"

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

	int32 AnchorGraftsNow()
	{
		static bool bRead = false;
		if (!bRead)
		{
			bRead = true;
			if (AnchorGraftsMode < 0 && WWOSwitches::Has(TEXT("WWOAnchorGrafts"))) AnchorGraftsMode = 1;
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

// --- Anchor grafts --------------------------------------------------------------------------------

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

void UWWOBodyComponent::ClearAnchorGrafts()
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
}

void UWWOBodyComponent::BuildAnchorGrafts()
{
	ClearAnchorGrafts();
	const TSharedPtr<FJsonObject> R = Recipe(AnchorFlora);
	const USkeletalMesh* Mesh = HasShells() ? Model->GetSkeletalMeshAsset() : nullptr;
	if (!R.IsValid() || !Mesh) return;
	const double Started = FPlatformTime::Seconds();
	// The same look every time for the same kit (as RebuildGrafts' procedural grafts).
	FString Key = AnchorGrenade.ToString();
	for (const FName& Id : AnchorIds) Key += TEXT("|") + Id.ToString();
	FRandomStream Random(int32(GetTypeHash(Key)));
	// One mesh per bone, built in the robot mesh's space in the reference pose (where the anchors are, and
	// where the builder's "down" is the body's down) and hung on that bone.
	TMap<FName, TUniquePtr<FWWOGraftBuilder>> Builders;
	auto BuilderFor = [&](FName Bone) -> FWWOGraftBuilder&
	{
		TUniquePtr<FWWOGraftBuilder>& B = Builders.FindOrAdd(Bone);
		if (!B)
		{
			B = MakeUnique<FWWOGraftBuilder>(FVector::ZeroVector, FLinearColor::White, WWO::M, Random);
			B->VineColor = FLinearColor(0.2f, 0.13f, 0.07f); // the seed pods' stalks (as BarkGrafts)
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
			for (int32 Slot = 0; Slot < NumShellSlots; ++Slot)
			{
				const FWWOShellAnchorSet* Set = WornAnchors(Slot);
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
	for (int32 Slot = 0; Slot < NumShellSlots; ++Slot)
	{
		if (const FWWOShellAnchorSet* Set = WornAnchors(Slot)) for (const TPair<FName, FWWOShellVolume>& It : Set->Obstacles) Obstacles.Add(&It.Value);
	}
	auto FindVolume = [&](FName Name) -> const FWWOShellVolume*
	{
		for (int32 Slot = 0; Slot < NumShellSlots; ++Slot)
		{
			const FWWOShellAnchorSet* Set = WornAnchors(Slot);
			if (const FWWOShellVolume* V = Set ? Set->Volumes.Find(Name) : nullptr) return V;
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
		FWWOGraftBuilder& B = BuilderFor(Bone);
		if (bPlate)
		{
			B.Plate(Center, N, Along, Length * Cm, Width * Cm, Curve * Cm, Num(O, TEXT("thickness"), 2.0) * Cm, Num(O, TEXT("lift"), 0.0) * Cm,
				Flag(O, TEXT("cracked"), true));
		}
		else
		{
			B.SeedPod(Center, Length * Cm, Width * Cm, Stalk * Cm, Hang);
		}
		++Pieces;
	};
	// VoltVine's pieces (FWWOVineGraft): made on the first one asked for.
	TUniquePtr<FWWOVineGraft> Vines;
	auto VinesNow = [&]() -> FWWOVineGraft&
	{
		if (!Vines)
		{
			Vines = MakeUnique<FWWOVineGraft>(Mesh, Random, VineJointsMode != 0);
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
	FString Kit;
	R->TryGetStringField(TEXT("kit"), Kit);
	TArray<FVector4> Glow;           // each cap's gills (where, how far their light reaches the mycelium)
	TArray<TFunction<void()>> Later; // the mycelium
	int32 KeptOff = 0;               // pieces left out to keep the face clear
	// One kit piece (or one per spot along a path), on one side.
	auto PlaceMesh = [&](const TSharedPtr<FJsonObject>& O, const TFunctionRef<FString(const FString&)> Named, float Sign)
	{
		FString MeshName, AnchorName, PathName, UpMode;
		O->TryGetStringField(TEXT("mesh"), MeshName);
		const FWWOLeafShape* Shape = Kit.IsEmpty() ? nullptr : FWWOVineGraft::MeshShape(FString::Printf(TEXT("%s/%s.%s"), *Kit, *MeshName, *MeshName));
		if (!Shape)
		{
			UE_LOG(LogWWO, Warning, TEXT("Graft recipes: no kit mesh '%s' in '%s'"), *MeshName, *Kit);
			++Missing;
			return;
		}
		// Where: an anchor, or along a path (every spacing_cm over 'range', or once at 'at').
		TArray<FWWOShellPoint> Spots;
		float SlideMax = 0.f, SizeScale = 1.f;
		const bool bPath = !O->TryGetStringField(TEXT("anchor"), AnchorName);
		if (!bPath)
		{
			const FWWOShellPoint* P = FindAnchorPoint(FName(*Named(AnchorName)));
			if (!P) { ++Missing; return; }
			Spots.Add(*P);
			SlideMax = P->Size;
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
		const bool bCap = !MeshName.Contains(TEXT("Mycelium")); // its gills light the mycelium round it
		for (int32 i = 0; i < Spots.Num(); ++i)
		{
			// The same draws in the same order every time: the same loadout grows the same way on any robot.
			FRandomStream Rs(Seed * 7919 + i * 131 + (Sign < 0.f ? 104729 : 0));
			const float JR = Rs.FRandRange(-JRoll, JRoll), JP = Rs.FRandRange(-JPitch, JPitch), JS = Rs.FRandRange(-JScale, JScale);
			const float SlideA = Rs.FRandRange(0.f, UE_TWO_PI), SlideD = FMath::Min(Rs.FRand() * JSlide, SlideMax);
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
				VinesNow().KitPiece(Bc, *Shape, Xf);
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
	if (!AnchorGrenade.IsNone())
	{
		Section(TEXT("grenade"));
		Section(TEXT("grenade.") + AnchorGrenade.ToString()); // this grenade's own pieces
	}
	for (const FName& Id : AnchorIds) Section(Id.ToString());
	for (const TFunction<void()>& Grow : Later) Grow();

	// Each piece is hung on its bone: its vertices are in the mesh's space, so the bone's reference pose is
	// undone (in that pose it sits exactly where it was built). NAME_None: on the model itself (the vines' joints).
	auto Hang = [&](UProceduralMeshComponent* C, FName Bone)
	{
		C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
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
	for (TPair<FName, TUniquePtr<FWWOGraftBuilder>>& It : Builders)
	{
		if (!It.Value || It.Value->IsEmpty()) continue;
		UProceduralMeshComponent* C = NewObject<UProceduralMeshComponent>(GetOwner());
		C->bUseAsyncCooking = false;
		It.Value->Build(C, C);
		Hang(C, It.Key);
	}
	int32 VineTris = 0, JointTris = 0, KitTris = 0, NetTris = 0;
	if (Vines)
	{
		for (TPair<FName, FWWOVineMesh>& It : Vines->Rigid)
		{
			if (It.Value.IsEmpty()) continue;
			UProceduralMeshComponent* C = NewObject<UProceduralMeshComponent>(GetOwner());
			C->bUseAsyncCooking = false;
			It.Value.Build(C);
			Hang(C, It.Key);
			VineTris += It.Value.NumTris();
		}
		if (!Vines->Joints->IsEmpty())
		{
			UProceduralMeshComponent* C = NewObject<UProceduralMeshComponent>(GetOwner());
			C->bUseAsyncCooking = false;
			VineJoints = Vines->Joints;
			Hang(C, NAME_None);
			VineJoints->Start(C, Model);
			JointTris = VineJoints->NumTris();
		}
		Pieces += Vines->Pieces;
		KitTris = Vines->KitTris;
		NetTris = Vines->NetTris;
	}
	UE_LOG(LogWWO, Log, TEXT("Body %s: anchor grafts for %s (%s) on %s, %s, %s, %s: %d pieces in %d meshes, %d anchors or paths missing, %d kept off the face, vines %d triangles rigid + %d in %d joint stations (kit pieces %d, mycelium %d), seated %d (mean gap %.2f -> %.2f cm), obstacles %d: %d pieces pushed, %d left out, %d net knots left out, %d vine points inside, %.2f ms"),
		*GetOwner()->GetName(), *AnchorFlora.ToString(), *Key, *WornParts[ShellHead].ToString(), *WornParts[ShellCore].ToString(),
		*WornParts[ShellArms].ToString(), *WornParts[ShellLegs].ToString(), Pieces, AnchorParts.Num(), Missing, KeptOff, VineTris, JointTris,
		VineJoints ? VineJoints->Stations.Num() : 0, KitTris, NetTris, Seated, Seated ? SeatGapBefore / Seated : 0.f, Seated ? SeatGapAfter / Seated : 0.f,
		Obstacles.Num(), ObstaclePushed, ObstacleLeftOut, Vines ? Vines->KnotsBlocked : 0, VineInObstacles, (FPlatformTime::Seconds() - Started) * 1000.0);
}
