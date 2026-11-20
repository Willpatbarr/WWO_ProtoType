// The body's graft anchors (the worn shell parts' anchors and paths, Characters/WWOShellAnchors.h) and the
// grafts placed from them: a flora's pieces from Config/GraftRecipes.json, built as procedural bark plates
// and seed pods (FWWOGraftBuilder) on whatever parts are worn, and placed again whenever a part is swapped.

#include "Characters/WWOBodyComponent.h"
#include "Characters/WWOShellAnchors.h"
#include "Characters/WWOGraftBuilder.h"
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
	auto Section = [&](const FString& Name)
	{
		const TArray<TSharedPtr<FJsonValue>>* List = nullptr;
		if (!R->TryGetArrayField(Name, List)) return;
		for (const TSharedPtr<FJsonValue>& V : *List)
		{
			const TSharedPtr<FJsonObject> O = V->AsObject();
			if (!O.IsValid()) continue;
			FString AnchorName, PathName;
			O->TryGetStringField(TEXT("anchor"), AnchorName);
			O->TryGetStringField(TEXT("path"), PathName);
			const FString& Template = AnchorName.IsEmpty() ? PathName : AnchorName;
			// "{s}": once for each side, l then r.
			TArray<TPair<FString, float>> Sides;
			if (Template.Contains(TEXT("{s}"))) Sides = { { Template.Replace(TEXT("{s}"), TEXT("l")), 1.f }, { Template.Replace(TEXT("{s}"), TEXT("r")), -1.f } };
			else Sides = { { Template, 1.f } };
			for (const TPair<FString, float>& Side : Sides)
			{
				if (!AnchorName.IsEmpty())
				{
					const FWWOShellPoint* P = FindAnchorPoint(FName(*Side.Key));
					if (!P) { ++Missing; continue; }
					// scale_by_size: as big as the spot it sits on allows (size_ref cm is the recipe's own size).
					const float Scale = Flag(O, TEXT("scale_by_size"), false) ? FMath::Clamp(P->Size / float(Num(O, TEXT("size_ref"), 5.0)), 0.6f, 1.6f) : 1.f;
					Place(*P, Scale, Side.Value, O);
				}
				else
				{
					const FWWOShellPath* Path = FindPath(FName(*Side.Key));
					if (!Path) { ++Missing; continue; }
					const FVector Range = WWOConfig::Vec(O, TEXT("range"), FVector(0.0, 1.0, 0.0));
					TArray<FWWOShellPoint> Samples;
					WWOShellAnchors::Sample(*Path, Mesh, float(Num(O, TEXT("spacing_cm"), 8.0)), float(Range.X), float(Range.Y), Samples);
					for (const FWWOShellPoint& P : Samples) Place(P, 1.f, Side.Value, O);
				}
			}
		}
	};
	if (!AnchorGrenade.IsNone()) Section(TEXT("grenade"));
	for (const FName& Id : AnchorIds) Section(Id.ToString());

	for (TPair<FName, TUniquePtr<FWWOGraftBuilder>>& It : Builders)
	{
		if (!It.Value || It.Value->IsEmpty()) continue;
		UProceduralMeshComponent* C = NewObject<UProceduralMeshComponent>(GetOwner());
		C->bUseAsyncCooking = false;
		It.Value->Build(C, C);
		C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		C->LightingChannels = Model->LightingChannels; // lit like the body (the menu preview has its own lights)
		C->SetupAttachment(Model, It.Key);
		// Its vertices are in the mesh's space: undo the bone's reference pose, so in that pose it sits exactly there.
		C->SetRelativeTransform(WWOShellAnchors::RefBone(Mesh, It.Key).Inverse());
		C->RegisterComponent();
		GetOwner()->AddInstanceComponent(C);
		ApplyShadowOnly(C); // your own: hidden from you, casting your shadow, as the model is
		C->bCastHiddenShadow = bShadowOnly;
		Grafts.Add(C);
		AnchorParts.Add(C);
	}
	UE_LOG(LogWWO, Log, TEXT("Body %s: anchor grafts for %s on %s, %s, %s, %s: %d pieces on %d bones, %d anchors or paths missing, %.2f ms"),
		*GetOwner()->GetName(), *AnchorFlora.ToString(), *WornParts[ShellHead].ToString(), *WornParts[ShellCore].ToString(),
		*WornParts[ShellArms].ToString(), *WornParts[ShellLegs].ToString(), Pieces, AnchorParts.Num(), Missing, (FPlatformTime::Seconds() - Started) * 1000.0);
}
