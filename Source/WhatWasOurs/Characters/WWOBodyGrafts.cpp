// The body's frame part colors and flora grafts (Godot: character_model.gd show_frame_parts,
// show_flora, _volt_grafts, _spore_grafts, _bark_grafts). Each graft is built around a bone's
// point in the standing pose and pinned to that bone, so it moves with the animations. The Godot
// code's bone names (the UAL skeleton) map to the mannequin's as in Tools/bake_ual_anims.py.

#include "Characters/WWOBodyComponent.h"
#include "Characters/WWOGraftBuilder.h"
#include "FX/WWOFX.h"
#include "Systems/WWOLoadoutSubsystem.h"
#include "Data/WWOData.h"
#include "WWOTypes.h"
#include "ProceduralMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"

// The body's own directions and scale while the grafts are placed.
struct FWWOGraftFrame
{
	FVector Right, Forward, Up;
	float U = 100.f; // cm per Godot meter (times the body's scale)
	FVector Down() const { return -Up; }
	mutable FRandomStream Random;
	TArray<FName> Ids;
	FName Grenade;
	bool Has(FName Id) const { return Ids.Contains(Id); }
};

namespace
{
	constexpr float GrenadeRadius = 0.065f;
	constexpr float SeedRadius = 0.035f;
	const FLinearColor Mycelium(0.55f, 0.42f, 0.66f);
	const FLinearColor MyceliumGlow(0.45f, 0.15f, 0.8f, 0.15f); // rgb + strength
	const FLinearColor Stalk(0.2f, 0.13f, 0.07f);

	FName Sided(const TCHAR* Bone, const TCHAR* Side) { return FName(*FString::Printf(TEXT("%s_%s"), Bone, Side)); }
}

FName UWWOBodyComponent::UnrealBone(FName GodotBone)
{
	// The UAL skeleton has three spine bones, the mannequin five (bake_ual_anims.py).
	static const TMap<FName, FName> Map = { { "spine_02", "spine_03" }, { "spine_03", "spine_05" }, { "Head", "head" } };
	const FName* Found = Map.Find(GodotBone);
	return Found ? *Found : GodotBone;
}

void UWWOBodyComponent::AttachBuilt(FName GodotBone, const FWWOGraftBuilder& Builder)
{
	// CharacterModel._attach_mesh: the mesh is built around Builder.Origin (world, standing pose).
	if (Builder.IsEmpty() || !GetMesh()) return;
	UProceduralMeshComponent* C = NewObject<UProceduralMeshComponent>(GetOwner());
	C->bUseAsyncCooking = false;
	Builder.Build(C, C);
	Pin(C, UnrealBone(GodotBone), FTransform(Builder.Origin), true);
}

void UWWOBodyComponent::AttachBall(FName GodotBone, const FVector& At, float RadiusM, const FLinearColor& Color)
{
	// CharacterModel._attach_ball: a plain sphere (roughness 0.5, no glow).
	AddShape(UnrealBone(GodotBone), At, WWOFX::Sphere(), FVector(RadiusM * 2.f * ModelScale), Color, 0.f, true);
}

void UWWOBodyComponent::AttachModel(FName GodotBone, const TCHAR* MeshName, const FVector& At, float AngleRad, float Scale, const FWWOGraftFrame& F)
{
	// CharacterModel._attach_model: a nature pack mushroom, turned AngleRad about up (Godot's
	// Basis(UP, angle), measured on the body facing its own front) and scaled.
	UStaticMesh* Asset = LoadObject<UStaticMesh>(nullptr, *FString::Printf(TEXT("/Game/WWO/Meshes/%s.%s"), MeshName, MeshName));
	if (!Asset || !GetMesh()) return;
	UStaticMeshComponent* C = NewObject<UStaticMeshComponent>(GetOwner());
	C->SetStaticMesh(Asset);
	// The import keeps glTF's (x, z, y); Godot's own axes are (-z, x, y) here, a quarter turn on.
	// Godot's turn about up is mirrored in Unreal's axes.
	const FQuat Body = FRotationMatrix::MakeFromXZ(F.Forward, F.Up).ToQuat();
	const FQuat Turn(FVector::UpVector, UE_HALF_PI - AngleRad);
	Pin(C, UnrealBone(GodotBone), FTransform(Body * Turn, At, FVector(Scale * ModelScale)), true);
	if (UMaterialInstanceDynamic* M = C->CreateAndSetMaterialInstanceDynamic(0))
	{
		M->SetVectorParameterValue("Glow", FLinearColor(0.25f, 0.35f, 1.f));
	}
}

void UWWOBodyComponent::RebuildGrafts()
{
	// show_loadout: the frame part colors, then the flora grafts.
	for (UPrimitiveComponent* C : Grafts)
	{
		if (C) C->DestroyComponent();
	}
	Grafts.Reset();
	const UWWOLoadoutSubsystem* Loadout = UWWOLoadoutSubsystem::Get(this);
	USkeletalMeshComponent* Mesh = GetMesh();
	if (!Loadout || !Mesh) return;
	// A body following the loadout shows its flora's character model.
	if (bFollowsLoadout && Loadout->GetFlora()) SetModel(Loadout->GetFlora()->Id);

	// show_frame_parts: each region takes the color of the frame part equipped in that slot.
	static const FName Slots[NumRegions] = { "head", "arms", "core", "legs" };
	for (int32 r = 0; r < NumRegions; ++r)
	{
		const UWWOFramePartData* Part = Loadout->GetFramePart(Slots[r]);
		RegionColors[r] = Part ? Part->BodyColor(BodyColor) : BodyColor;
	}
	bRegionColors = true;
	ApplyColors();

	// show_flora.
	FWWOGraftFrame F;
	for (int32 Slot = 0; Slot < UWWOLoadoutSubsystem::AbilitySlots; ++Slot)
	{
		if (const UWWOAbilityData* A = Loadout->GetAbility(Slot)) F.Ids.Add(A->Id);
	}
	const UWWOFloraData* Flora = Loadout->GetFlora();
	const UWWOGrenadeData* Grenade = Loadout->GetGrenade();
	const FName FloraId = Flora ? Flora->Id : NAME_None;
	F.Grenade = Grenade ? Grenade->Id : NAME_None;
	FLinearColor GrenadeColor = Grenade ? Grenade->Color : FLinearColor::White;
	// A flora can give every graft one color (VoltVine: all blue).
	const FLinearColor Graft = Flora ? Flora->GraftColor : FLinearColor(0, 0, 0, 0);
	if (Graft.A > 0.f) GrenadeColor = Graft;
	// The mannequin faces +Y in its own space, its right is -X.
	const FTransform MeshTransform = Mesh->GetComponentTransform();
	F.Right = MeshTransform.TransformVectorNoScale(FVector(-1.f, 0.f, 0.f));
	F.Forward = MeshTransform.TransformVectorNoScale(FVector(0.f, 1.f, 0.f));
	F.Up = MeshTransform.TransformVectorNoScale(FVector(0.f, 0.f, 1.f));
	F.U = WWO::M * ModelScale;
	// The same look every time for the same abilities and grenade (Godot: seed(hash(ids) + ...)).
	FString Key = F.Grenade.ToString();
	for (const FName& Id : F.Ids) Key += TEXT("|") + Id.ToString();
	F.Random.Initialize(int32(GetTypeHash(Key)));

	const FVector Hips = GodotBonePoint("pelvis");
	if (FloraId == "voltvine")
	{
		VoltGrafts(F, GrenadeColor);
	}
	else if (Grenade && FloraId != "veilspore" && FloraId != "ironbark")
	{
		AttachBall("pelvis", Hips + F.Right * 0.17f * F.U + F.Forward * 0.04f * F.U, GrenadeRadius, GrenadeColor);
	}
	if (F.Has("seedburst"))
	{
		for (int32 i = 0; i < 3; ++i) // in a row along the belt
		{
			AttachBall("pelvis", Hips - F.Right * 0.165f * F.U + F.Forward * (0.07f - 0.07f * i) * F.U, SeedRadius, GrenadeColor);
		}
	}
	if (FloraId == "veilspore") SporeGrafts(F);
	if (FloraId == "ironbark") BarkGrafts(F);
}

void UWWOBodyComponent::VoltGrafts(const FWWOGraftFrame& F, const FLinearColor& PodColor)
{
	// VoltVine: vines wound around the limbs with serrated leaves and blue crystal pods.
	// - Grenade: a leafy vine skirt around the hips, with the grenade as pods on the right hip
	//   (Volt Grenade one big pod, Shock Pool two, Snare Mine three small ones).
	// - Overgrowth: vines and pods down both shins.
	// - Thornfist: thorny vines and pods around both forearms.
	// - Static Charge: vines around the upper arms, leaves over the shoulders, pods.
	// - Melee Guard: a vine band across each hand with tiny pods on the knuckles.
	const float U = F.U;
	FRandomStream& R = F.Random;
	const FVector Hips = GodotBonePoint("pelvis");
	using G = FWWOGraftBuilder;
	if (!F.Grenade.IsNone())
	{
		G Belt(Hips, PodColor, U, R);
		const FVector BeltCenter = Hips + F.Up * 0.02f * U;
		const TArray<FVector> Ring = Belt.Loop(BeltCenter, F.Right * 0.18f * U, F.Forward * 0.14f * U, 0.012f, 0.012f);
		Belt.Loop(BeltCenter + F.Down() * 0.025f * U, F.Right * 0.175f * U, F.Forward * 0.135f * U, 0.009f, 0.01f);
		// Big fronds splaying out and down from the belt, gappy rather than a full skirt, and none
		// across the front middle. The ones over each leg are pinned to that thigh so they swing
		// with it instead of cutting through it.
		G ThighL(GodotBonePoint("thigh_l"), PodColor, U, R), ThighR(GodotBonePoint("thigh_r"), PodColor, U, R);
		for (int32 i = 0; i < 14; ++i)
		{
			const FVector At = Ring[int32((i + 0.5f) * (Ring.Num() - 1) / 14.f)];
			const FVector Out = (At - BeltCenter).GetSafeNormal();
			const float Side = FVector::DotProduct(Out, F.Right);
			const float Front = FVector::DotProduct(Out, F.Forward);
			if (FMath::Abs(Side) < 0.4f && Front > 0.f) continue; // leave the front open
			G& Builder = FMath::Abs(Side) < 0.4f ? Belt : (Side > 0.f ? ThighR : ThighL);
			const FVector Jitter = F.Right * R.FRandRange(-0.15f, 0.15f) + F.Forward * R.FRandRange(-0.15f, 0.15f);
			const FVector Dir = (F.Down() + Out * 0.95f + Jitter).GetSafeNormal();
			const float Len = R.FRandRange(0.22f, 0.28f);
			Builder.AddLeaf(At, Dir, Len, G::Frond, 0.4f, 0.35f, R.FRandRange(-0.35f, 0.35f));
			if (i % 2 == 0) // a smaller frond sprouting up and out, to break up the outline (stays with the hips)
			{
				const float Short = R.FRandRange(0.11f, 0.14f);
				Belt.AddLeaf(At, (F.Up * 0.6f + Out).GetSafeNormal(), Short, G::FrondShort, 0.35f, 0.5f, R.FRandRange(-0.4f, 0.4f));
			}
		}
		AttachBuilt("thigh_l", ThighL);
		AttachBuilt("thigh_r", ThighR);
		const FVector Hip = Hips + F.Right * 0.19f * U + F.Forward * 0.02f * U + F.Down() * 0.04f * U;
		const int32 Count = F.Grenade == "shock_pool" ? 2 : (F.Grenade == "snare_mine" ? 3 : 1);
		for (int32 i = 0; i < Count; ++i)
		{
			const float Size = Count == 1 ? 1.f : (Count == 2 ? 0.72f : 0.55f);
			const FVector Spot = Hip + F.Forward * (i - (Count - 1) * 0.5f) * 0.05f * U;
			Belt.AddPod(Spot + F.Right * 0.01f * U, F.Down(), F.Right, 0.12f * Size, 0.04f * Size);
		}
		AttachBuilt("pelvis", Belt);
	}
	for (const TCHAR* S : { TEXT("l"), TEXT("r") })
	{
		const FVector Outward = FCString::Strcmp(S, TEXT("r")) == 0 ? F.Right : -F.Right;
		if (F.Has("overgrowth"))
		{
			const FVector Knee = GodotBonePoint(Sided(TEXT("calf"), S)), Ankle = GodotBonePoint(Sided(TEXT("foot"), S));
			G B(Knee, PodColor, U, R);
			const FVector A0 = FMath::Lerp(Knee, Ankle, 0.08f), A1 = FMath::Lerp(Knee, Ankle, 0.92f);
			const TArray<FVector> Vine = B.Wrap(A0, A1, 0.062f, 2.2f, 0.3f, 0.011f);
			B.Wrap(A0, A1, 0.055f, 1.6f, 2.6f, 0.007f);
			B.LeavesAlong(Vine, A0, A1, 9, 0.085f);
			for (const float T : { 0.3f, 0.65f }) B.AddPod(FMath::Lerp(Knee, Ankle, T) + F.Forward * 0.062f * U, Ankle - Knee, F.Forward, 0.1f, 0.034f);
			AttachBuilt(Sided(TEXT("calf"), S), B);
		}
		if (F.Has("thornfist"))
		{
			const FVector Elbow = GodotBonePoint(Sided(TEXT("lowerarm"), S)), Wrist = GodotBonePoint(Sided(TEXT("hand"), S));
			G B(Elbow, PodColor, U, R);
			const FVector A0 = FMath::Lerp(Elbow, Wrist, 0.05f), A1 = FMath::Lerp(Elbow, Wrist, 0.95f);
			const FVector Axis = (A1 - A0).GetSafeNormal();
			const TArray<FVector> Vine = B.Wrap(A0, A1, 0.062f, 2.f, 0.f, 0.011f);
			const TArray<FVector> Vine2 = B.Wrap(A0, A1, 0.06f, 2.f, UE_PI, 0.008f);
			for (int32 i = 3; i < Vine2.Num() - 2; i += 4) // thorns sticking out of the second vine
			{
				const FVector Center = A0 + Axis * FVector::DotProduct(Vine2[i] - A0, Axis);
				B.Thorn(Vine2[i], (Vine2[i] - Center).GetSafeNormal() + Axis * 0.4f);
			}
			B.LeavesAlong(Vine, A0, A1, 6, 0.07f, 0.9f);
			for (const float T : { 0.3f, 0.68f }) B.AddPod(FMath::Lerp(Elbow, Wrist, T) + Outward * 0.064f * U, Wrist - Elbow, Outward, 0.085f, 0.028f);
			AttachBuilt(Sided(TEXT("lowerarm"), S), B);
		}
		if (F.Has("static_charge"))
		{
			const FVector Shoulder = GodotBonePoint(Sided(TEXT("upperarm"), S)), Elbow = GodotBonePoint(Sided(TEXT("lowerarm"), S));
			G B(Shoulder, PodColor, U, R);
			const FVector A0 = FMath::Lerp(Shoulder, Elbow, 0.12f), A1 = FMath::Lerp(Shoulder, Elbow, 0.92f);
			const TArray<FVector> Vine = B.Wrap(A0, A1, 0.072f, 1.6f, 1.f, 0.011f);
			B.LeavesAlong(Vine, A0, A1, 6, 0.075f);
			for (int32 i = 0; i < 5; ++i) // fronds draped over the shoulder
			{
				const float Spread = (i - 2) * 0.35f;
				const FVector Dir = (Outward * 0.8f + F.Down() * 0.15f + F.Forward * Spread).GetSafeNormal();
				B.AddLeaf(Shoulder + F.Up * 0.07f * U - Outward * 0.03f * U + F.Forward * Spread * 0.05f * U, Dir, 0.13f, G::Frond, 0.4f, 0.45f, Spread * 0.3f);
			}
			for (const float T : { 0.4f, 0.72f }) B.AddPod(FMath::Lerp(Shoulder, Elbow, T) + Outward * 0.075f * U, Elbow - Shoulder, Outward, 0.09f, 0.03f);
			AttachBuilt(Sided(TEXT("upperarm"), S), B);
		}
		if (F.Has("melee_guard"))
		{
			const FVector Hand = GodotBonePoint(Sided(TEXT("hand"), S)), Knuckles = GodotBonePoint(Sided(TEXT("middle_01"), S));
			G B(Hand, PodColor, U, R);
			const FVector Back = Outward; // the back of the hand faces out in the standing pose
			B.Wrap(FMath::Lerp(Hand, Knuckles, 0.15f), FMath::Lerp(Hand, Knuckles, 0.85f), 0.032f, 1.5f, 0.f, 0.006f);
			for (int32 i = 0; i < 3; ++i)
			{
				const FVector Spot = FMath::Lerp(Hand, Knuckles, 0.75f) + F.Forward * (i - 1) * 0.024f * U + Back * 0.025f * U;
				B.AddPod(Spot, Knuckles - Hand, Back, 0.03f, 0.011f, false);
			}
			B.AddLeaf(Hand + Back * 0.03f * U, (Hand - Knuckles).GetSafeNormal() + Back * 0.4f, 0.06f, G::Blade, 0.3f, 0.1f);
			AttachBuilt(Sided(TEXT("hand"), S), B);
		}
	}
}

void UWWOBodyComponent::SporeGrafts(const FWWOGraftFrame& F)
{
	// VeilSpore: pale mycelium veins spreading over the body, with shelf mushrooms (dark purple
	// caps, glowing blue gills) growing out of it. Lopsided like the concept art:
	// - Spore Fog Damage: a hood of shelves over the right shoulder and head, with mycelium
	//   spreading across the chest, up the neck and down the right arm.
	// - Spore Dash: mycelium and a stack of shelves down the left leg.
	// - Spore Patches: mycelium round the back of the waist with little mushrooms on it.
	// - Grenade: a cluster of shelf fungi on the right hip.
	const float U = F.U;
	FRandomStream& R = F.Random;
	const FLinearColor Glow(0.25f, 0.35f, 1.f);
	auto Spore = [&](const FVector& Center)
	{
		FWWOGraftBuilder B(Center, Glow, U, R);
		B.VineColor = Mycelium;
		B.VineGlow = MyceliumGlow;
		return B;
	};
	const FVector Right = F.Right, Forward = F.Forward, Up = F.Up, Back = -F.Forward;
	const FVector Hips = GodotBonePoint("pelvis");
	const FVector Neck = GodotBonePoint("neck_01");
	const FVector Head = GodotBonePoint("Head");
	const FVector HeadCenter = Head + Up * 0.12f * U;
	const FVector Waist = GodotBonePoint("spine_01");
	if (F.Has("spore_fog_damage"))
	{
		FWWOGraftBuilder Chest = Spore(GodotBonePoint("spine_03"));
		for (int32 i = 0; i < 14; ++i) // veins spread over the right half of the chest and back
		{
			const FVector2D Start(R.FRandRange(0.45f, 0.97f), R.FRandRange(-1.5f, 1.6f));
			const float Heading = R.FRand() * UE_TWO_PI;
			Chest.Web(Waist, Neck, Right, Forward, 0.2f, 0.135f, Start, Heading, R.FRandRange(0.2f, 0.35f), 0.0055f);
		}
		const FVector Shoulder = GodotBonePoint("upperarm_r"), Elbow = GodotBonePoint("lowerarm_r");
		Chest.Shelf(Shoulder + Up * 0.1f * U + Right * 0.02f * U, Right + Up * 0.15f, Up, 0.32f, 0.2f, 0.04f);
		Chest.Shelf(Neck + Right * 0.1f * U + Up * 0.03f * U, Right * 0.8f + Up * 0.35f + Back * 0.2f, Up, 0.25f, 0.16f, 0.035f);
		AttachBuilt("spine_03", Chest);
		FWWOGraftBuilder Arm = Spore(Shoulder);
		for (int32 i = 0; i < 5; ++i)
		{
			const FVector2D Start(0.f, R.FRandRange(-1.2f, 1.2f));
			const float Heading = R.FRandRange(-0.6f, 0.6f);
			Arm.Web(Shoulder, Elbow, Right, Forward, 0.068f, 0.068f, Start, Heading, R.FRandRange(0.25f, 0.35f), 0.006f);
		}
		Arm.Shelf(FMath::Lerp(Shoulder, Elbow, 0.22f) + Right * 0.07f * U, Right - Up * 0.05f, Up, 0.26f, 0.17f, 0.035f);
		Arm.Shelf(FMath::Lerp(Shoulder, Elbow, 0.52f) + Right * 0.07f * U, Right - Up * 0.1f + Forward * 0.15f, Up, 0.19f, 0.13f, 0.03f);
		AttachBuilt("upperarm_r", Arm);
		FWWOGraftBuilder Hood = Spore(Head);
		const FVector Top = Head + Up * 0.25f * U;
		for (int32 i = 0; i < 6; ++i) // veins climbing the right side and back of the head
		{
			const FVector2D Start(0.f, R.FRandRange(-1.6f, 0.4f));
			const float Heading = R.FRandRange(-0.3f, 0.3f);
			Hood.Web(Head, Top, Right, Forward, 0.115f, 0.125f, Start, Heading, R.FRandRange(0.15f, 0.25f), 0.006f);
		}
		Hood.Shelf(HeadCenter + Up * 0.12f * U + Back * 0.08f * U, Forward * 0.5f + Right * 0.6f + Up * 0.1f, Up, 0.34f, 0.2f, 0.04f);
		Hood.Shelf(HeadCenter + Right * 0.11f * U + Up * 0.03f * U + Back * 0.02f * U, Right - Up * 0.05f, Up, 0.27f, 0.17f, 0.035f);
		Hood.Shelf(HeadCenter + Up * 0.06f * U + Back * 0.12f * U, Back * 0.8f + Up * 0.3f + Right * 0.4f, Up, 0.28f, 0.16f, 0.035f);
		AttachBuilt("Head", Hood);
	}
	if (F.Has("spore_dash"))
	{
		const FVector Hip = GodotBonePoint("thigh_l"), Knee = GodotBonePoint("calf_l"), Ankle = GodotBonePoint("foot_l");
		FWWOGraftBuilder Thigh = Spore(Hip);
		for (int32 i = 0; i < 8; ++i)
		{
			const FVector2D Start(0.05f, UE_PI + R.FRandRange(-1.6f, 1.6f));
			const float Heading = R.FRandRange(-0.6f, 0.6f);
			Thigh.Web(Hip, Knee, Right, Forward, 0.1f, 0.1f, Start, Heading, R.FRandRange(0.35f, 0.5f), 0.0065f);
		}
		for (int32 k = 0; k < 3; ++k) // shelves stepping down the outside of the thigh
		{
			const float T = 0.22f + k * 0.2f;
			Thigh.Shelf(FMath::Lerp(Hip, Knee, T) - Right * 0.095f * U, -Right + Forward * 0.25f, Up, 0.22f - k * 0.03f, 0.14f - k * 0.018f, 0.03f);
		}
		AttachBuilt("thigh_l", Thigh);
		FWWOGraftBuilder Calf = Spore(Knee);
		for (int32 i = 0; i < 6; ++i)
		{
			const FVector2D Start(0.f, UE_PI + R.FRandRange(-1.6f, 1.6f));
			const float Heading = R.FRandRange(-0.6f, 0.6f);
			Calf.Web(Knee, Ankle, Right, Forward, 0.075f, 0.075f, Start, Heading, R.FRandRange(0.3f, 0.45f), 0.006f);
		}
		Calf.Shelf(FMath::Lerp(Knee, Ankle, 0.28f) - Right * 0.075f * U, -Right + Forward * 0.2f, Up, 0.15f, 0.1f, 0.028f);
		AttachBuilt("calf_l", Calf);
	}
	if (F.Has("spore_patches"))
	{
		FWWOGraftBuilder Belt = Spore(Hips);
		for (int32 i = 0; i < 5; ++i) // veins wrapping round the back of the waist
		{
			const FVector2D Start(0.4f, -UE_HALF_PI + R.FRandRange(-1.f, 1.f));
			const float Heading = UE_HALF_PI * (R.FRand() < 0.5f ? 1.f : -1.f);
			Belt.Web(Hips + F.Down() * 0.04f * U, Hips + Up * 0.14f * U, Right, Forward, 0.18f, 0.14f, Start, Heading, R.FRandRange(0.25f, 0.4f), 0.008f);
		}
		AttachBuilt("pelvis", Belt);
		for (int32 i = 0; i < 3; ++i) // little mushroom clumps on the back of the belt
		{
			const FVector Spot = Hips + Back * 0.14f * U + Right * (i - 1) * 0.1f * U + Up * 0.05f * U;
			AttachModel("pelvis", TEXT("SM_Mushroom_Common"), Spot, R.FRand() * UE_TWO_PI, 0.26f, F);
		}
	}
	if (!F.Grenade.IsNone())
	{
		const FVector Spot = Hips + Right * 0.22f * U + Forward * 0.02f * U + F.Down() * 0.04f * U;
		AttachModel("pelvis", TEXT("SM_Mushroom_Laetiporus"), Spot, -UE_HALF_PI, 0.17f, F);
		FWWOGraftBuilder Patch = Spore(Hips);
		for (int32 i = 0; i < 3; ++i)
		{
			const FVector2D Start(0.5f, R.FRandRange(-0.4f, 0.4f));
			const float Heading = R.FRandRange(0.f, UE_TWO_PI);
			Patch.Web(Hips + F.Down() * 0.1f * U, Hips + Up * 0.1f * U, Right, Forward, 0.18f, 0.14f, Start, Heading, R.FRandRange(0.15f, 0.25f), 0.007f);
		}
		AttachBuilt("pelvis", Patch);
	}
}

void UWWOBodyComponent::BarkGrafts(const FWWOGraftFrame& F)
{
	// IronBark: armor of chunky bark plates with glowing orange cracks (BarkGraft).
	// - Grenade: two orange seed pods hanging from the front of the belt.
	// - Extra Grenade: two more on the other side.
	// - Shield Share: overlapping bark scales in a V down the chest, layered plates over both
	//   shoulders and a collar rising round the neck.
	// - Root Bombs: bark vambraces on the forearms and greaves on the shins.
	const float U = F.U;
	FRandomStream& R = F.Random;
	auto BarkB = [&](const FVector& Center)
	{
		FWWOGraftBuilder B(Center, FLinearColor::White, U, R);
		B.VineColor = Stalk;
		return B;
	};
	const FVector Right = F.Right, Forward = F.Forward, Up = F.Up;
	const FVector Hips = GodotBonePoint("pelvis");
	if (!F.Grenade.IsNone() || F.Has("extra_grenade"))
	{
		FWWOGraftBuilder Belt = BarkB(Hips);
		TArray<float> Sides;
		if (!F.Grenade.IsNone()) Sides.Add(-1.f); // the concept hangs them left of center
		if (F.Has("extra_grenade")) Sides.Add(1.f);
		for (const float Side : Sides)
		{
			for (int32 k = 0; k < 2; ++k)
			{
				const FVector Top = Hips + Forward * 0.175f * U + Right * Side * (0.03f + k * 0.05f) * U + Up * 0.03f * U;
				Belt.SeedPod(Top, 0.11f - k * 0.015f, 0.04f - k * 0.005f, 0.045f + k * 0.03f);
			}
		}
		AttachBuilt("pelvis", Belt);
	}
	if (F.Has("shield_share"))
	{
		const FVector Waist = GodotBonePoint("spine_01"), Neck = GodotBonePoint("neck_01");
		const FVector Axis = (Neck - Waist).GetSafeNormal();
		FWWOGraftBuilder Chest = BarkB(GodotBonePoint("spine_03"));
		FVector Spot, N;
		// Rows of scales from the collarbone down, each pointing down and in toward the middle of
		// the chest (a V), the lower rows tucked under the upper ones.
		for (int32 Row = 0; Row < 3; ++Row)
		{
			const float T = 0.92f - Row * 0.17f;
			for (int32 j = 0; j < 3; ++j)
			{
				for (const float Side : { -1.f, 1.f })
				{
					const float Angle = UE_HALF_PI - Side * (0.28f + j * 0.48f) - Side * Row * 0.06f;
					FWWOGraftBuilder::OnOval(Waist, Neck, Right, Forward, 0.2f, 0.14f, T, Angle, U, Spot, N);
					const FVector Inward = -Right * Side * 0.55f;
					const FVector DownIn = (-Axis + Inward).GetSafeNormal();
					const bool bCracked = R.FRand() < 0.45f;
					Chest.Plate(Spot, N, DownIn, 0.2f - Row * 0.015f, 0.12f, 0.35f, 0.02f, 0.03f, bCracked);
				}
			}
		}
		// The center ridge where the V meets.
		for (int32 Row = 0; Row < 3; ++Row)
		{
			FWWOGraftBuilder::OnOval(Waist, Neck, Right, Forward, 0.2f, 0.14f, 0.8f - Row * 0.15f, UE_HALF_PI, U, Spot, N);
			Chest.Plate(Spot, N, -Axis, 0.16f, 0.07f, 0.3f, 0.02f, 0.02f, true);
		}
		// A collar of plates rising round the front and sides of the neck.
		for (int32 j = 0; j < 5; ++j)
		{
			const float Angle = UE_HALF_PI + (j - 2) * 0.5f;
			FWWOGraftBuilder::OnOval(Waist, Neck, Right, Forward, 0.12f, 0.1f, 1.05f, Angle, U, Spot, N);
			Chest.Plate(Spot + Axis * 0.04f * U, (N + Axis * 0.3f).GetSafeNormal(), (Axis + N * 0.4f).GetSafeNormal(), 0.11f, 0.08f, 0.2f, 0.016f, 0.015f, false);
		}
		AttachBuilt("spine_03", Chest);
		for (const TCHAR* S : { TEXT("l"), TEXT("r") }) // layered pauldrons stepping down each shoulder
		{
			const FVector Outward = FCString::Strcmp(S, TEXT("r")) == 0 ? Right : -Right;
			const FVector Shoulder = GodotBonePoint(Sided(TEXT("upperarm"), S)), Elbow = GodotBonePoint(Sided(TEXT("lowerarm"), S));
			const FVector ArmDir = (Elbow - Shoulder).GetSafeNormal();
			FWWOGraftBuilder Pauldron = BarkB(Shoulder);
			for (int32 k = 0; k < 3; ++k)
			{
				const FVector At = Shoulder + Up * (0.07f - k * 0.05f) * U + Outward * (0.03f + k * 0.025f) * U;
				const FVector Out = (Up * (1.f - k * 0.3f) + Outward * (0.6f + k * 0.3f)).GetSafeNormal();
				Pauldron.Plate(At, Out, (Outward * 0.3f + ArmDir).GetSafeNormal(), 0.17f - k * 0.015f, 0.2f - k * 0.02f, 0.1f, 0.022f, 0.03f, k == 1);
			}
			AttachBuilt(Sided(TEXT("upperarm"), S), Pauldron);
		}
	}
	if (F.Has("root_bombs"))
	{
		for (const TCHAR* S : { TEXT("l"), TEXT("r") })
		{
			const FVector Outward = FCString::Strcmp(S, TEXT("r")) == 0 ? Right : -Right;
			const FVector Elbow = GodotBonePoint(Sided(TEXT("lowerarm"), S)), Wrist = GodotBonePoint(Sided(TEXT("hand"), S));
			FWWOGraftBuilder Arm = BarkB(Elbow);
			const FVector Mid = FMath::Lerp(Elbow, Wrist, 0.5f);
			Arm.Plate(Mid + Outward * 0.005f * U, Outward, Wrist - Elbow, FVector::Dist(Elbow, Wrist) / U * 1.05f, 0.2f, 0.065f, 0.026f, 0.f, true);
			Arm.Plate(FMath::Lerp(Elbow, Wrist, 0.2f) + Outward * 0.03f * U - Forward * 0.01f * U, (Outward + Up * 0.2f).GetSafeNormal(), Wrist - Elbow,
				0.14f, 0.12f, 0.08f, 0.02f, 0.015f, false);
			AttachBuilt(Sided(TEXT("lowerarm"), S), Arm);
			const FVector Knee = GodotBonePoint(Sided(TEXT("calf"), S)), Ankle = GodotBonePoint(Sided(TEXT("foot"), S));
			FWWOGraftBuilder Leg = BarkB(Knee);
			const FVector Shin = FMath::Lerp(Knee, Ankle, 0.42f);
			Leg.Plate(Shin + Forward * 0.005f * U, Forward, Ankle - Knee, FVector::Dist(Knee, Ankle) / U * 1.02f, 0.24f, 0.075f, 0.028f, 0.f, true);
			AttachBuilt(Sided(TEXT("calf"), S), Leg);
		}
	}
}
