// The body's graft anchors (the worn shell parts' anchors and paths, Characters/WWOShellAnchors.h) and the
// grafts placed from them: a flora's pieces from Config/GraftRecipes.json, built on whatever parts are worn
// and placed again whenever a part is swapped: procedural bark plates and seed pods (FWWOGraftBuilder), and
// VoltVine's vines along chains of paths with their leaves, tendrils and crystal pods (FWWOVineGraft).

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
	// One piece at one point. Sign is -1 on the right side of a "{s}" pair: its across offset is mirrored.
	auto Place = [&](const FWWOShellPoint& At, float Scale, float Sign, const TSharedPtr<FJsonObject>& O)
	{
		const FVector Across = FVector::CrossProduct(At.RefUp, At.RefN); // the anchor frame's Y (up x out)
		const FVector Off = WWOConfig::Vec(O, TEXT("offset"));
		const FVector Center = At.RefP + At.RefN * Off.X + Across * Off.Y * Sign + At.RefUp * Off.Z;
		FString Piece, AlongWord(TEXT("down"));
		O->TryGetStringField(TEXT("piece"), Piece);
		O->TryGetStringField(TEXT("along"), AlongWord);
		const FVector Along = AlongWord == TEXT("up") ? At.RefUp : AlongWord == TEXT("across") ? Across * Sign : -At.RefUp;
		const float Cm = 1.f / WWO::M; // the builder's sizes are meters
		FWWOGraftBuilder& B = BuilderFor(At.Bone);
		if (Piece == TEXT("plate"))
		{
			B.Plate(Center, At.RefN, Along, Num(O, TEXT("length"), 12.0) * Scale * Cm, Num(O, TEXT("width"), 8.0) * Scale * Cm,
				FMath::Max(Num(O, TEXT("curve"), 30.0) * Scale, 1.0) * Cm, Num(O, TEXT("thickness"), 2.0) * Cm, Num(O, TEXT("lift"), 0.0) * Cm,
				Flag(O, TEXT("cracked"), true));
		}
		else if (Piece == TEXT("seed_pod"))
		{
			B.SeedPod(Center, Num(O, TEXT("length"), 10.0) * Scale * Cm, Num(O, TEXT("width"), 3.5) * Scale * Cm, Num(O, TEXT("stalk"), 4.0) * Cm);
		}
		else
		{
			UE_LOG(LogWWO, Warning, TEXT("Graft recipes: unknown piece '%s'"), *Piece);
			return;
		}
		++Pieces;
	};
	// VoltVine's pieces (FWWOVineGraft): made on the first one asked for.
	TUniquePtr<FWWOVineGraft> Vines;
	auto VinesNow = [&]() -> FWWOVineGraft&
	{
		if (!Vines) Vines = MakeUnique<FWWOVineGraft>(Mesh, Random, VineJointsMode != 0);
		return *Vines;
	};
	// A vine's line: its 'paths' one after another (each over 'range' [from, to] of its length; from > to runs it
	// backward), each next one joined to the last by a short lifted bridge (across a seam, where a Core path's end
	// and an Arms or Legs path's start lie a few cm apart on their own parts). False if a path is missing.
	auto VineLine = [&](const TSharedPtr<FJsonObject>& O, const TFunctionRef<FString(const FString&)> Named, FWWOVineLine& Line)
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
				const FVector P = FMath::Lerp(Pts[k].RefP, Pts[k + 1].RefP, T);
				const FVector N = FMath::Lerp(Pts[k].RefN, Pts[k + 1].RefN, T).GetSafeNormal();
				const FName Bone = T < 0.5f ? Pts[k].Bone : Pts[k + 1].Bone;
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
				if (!VineLine(O, Named, Line)) { ++Missing; return; }
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
	int32 VineTris = 0, JointTris = 0;
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
	}
	UE_LOG(LogWWO, Log, TEXT("Body %s: anchor grafts for %s (%s) on %s, %s, %s, %s: %d pieces in %d meshes, %d anchors or paths missing, vines %d triangles rigid + %d in %d joint stations, %.2f ms"),
		*GetOwner()->GetName(), *AnchorFlora.ToString(), *Key, *WornParts[ShellHead].ToString(), *WornParts[ShellCore].ToString(),
		*WornParts[ShellArms].ToString(), *WornParts[ShellLegs].ToString(), Pieces, AnchorParts.Num(), Missing, VineTris, JointTris,
		VineJoints ? VineJoints->Stations.Num() : 0, (FPlatformTime::Seconds() - Started) * 1000.0);
}
