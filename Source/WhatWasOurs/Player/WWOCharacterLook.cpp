// AWWOCharacter: what other machines see of this body: set up as yours or someone else's, their flora,
// look and lit fists (NetFlora / NetLook / NetFists, ShowLook), the PvP team tint and outline flash, the
// upper-body pose (SendPose), the body animation (UpdateModel) and someone else's footsteps.

#include "Player/WWOCharacter.h"
#include "WhatWasOurs.h"
#include "Player/WWOCharacterPrivate.h"
#include "WWOTypes.h"
#include "Engine/World.h"
#include "Systems/WWOSound.h"
#include "Systems/WWOWorldSubsystem.h"
#include "Data/WWOData.h"
#include "Player/WWOPlayerController.h"
#include "Player/WWOWeaponComponent.h"
#include "Player/WWOHealthComponent.h"
#include "Characters/WWOBodyComponent.h"
#include "Systems/WWOLoadoutSubsystem.h"
#include "GameFramework/CharacterMovementComponent.h"

using namespace WWOCharacter; // the constants these files share (WWOCharacterPrivate.h)

void AWWOCharacter::RefreshLocalVisuals()
{
	// A bot is run here but isn't anyone's view: it shows its whole body like another player.
	const bool bMine = IsLocalHuman();
	if (bMine != bLocalVisuals) SetupVisuals(bMine);
}

void AWWOCharacter::UpdateTeamTint()
{
	// PvP, on each machine: when the one playing here marks a player (the reticle comes over them,
	// or they enter the marker range while aimed in; UWWOWeaponComponent::UpdateMarks decides),
	// that player's outline flashes once: a strong outline over a faint tint, fading out. Red for
	// the other team, blue for theirs, whichever team that is. Nobody is tinted otherwise.
	static constexpr float Fill = 0.1f, Rim = 1.7f; // the outline saturates well in from the edge
	static constexpr float FlashHold = 0.25f, FlashFade = 0.55f; // seconds at full strength, then fading
	static const FLinearColor Foe(1.f, 0.1f, 0.06f), Mate(0.2f, 0.5f, 1.f);
	const APlayerController* PC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;
	const AWWOCharacter* Watcher = PC ? Cast<AWWOCharacter>(PC->GetPawn()) : nullptr;
	const double* Since = Watcher && Watcher != this ? Watcher->Weapon->Marked.Find(this) : nullptr;
	const float Age = Since ? (float)(GetWorld()->GetTimeSeconds() - *Since) : 99.f;
	const float Strength = 1.f - FMath::Clamp((Age - FlashHold) / FlashFade, 0.f, 1.f);
	if (bLocalVisuals || Health->bDead || !UWWOWorldSubsystem::IsPvP(this) || Strength <= 0.f)
	{
		Body->SetTeamTint(FLinearColor::Black, 0.f, 0.f);
		return;
	}
	Body->SetTeamTint(UWWOWorldSubsystem::CanHurt(Watcher, this) ? Foe : Mate, Fill * Strength, Rim * Strength);
}

void AWWOCharacter::SetupVisuals(bool bLocal)
{
	// Yours: the first-person gun and arms, and only your body's shadow (with your flora's grafts and
	// frame part colors). Someone else's (online): their whole body on their flora's model, no gun.
	bLocalVisuals = bLocal;
	if (GetNetMode() != NM_Standalone) UE_LOG(LogWWO, Display, TEXT("Net visuals: %s set up as %s"), *GetName(), bLocal ? TEXT("mine") : TEXT("someone else's"));
	Weapon->SetFirstPersonHidden(!bLocal);
	if (bLocal)
	{
		// A joiner's own character can be set up as someone else's for a moment before it's known to
		// be theirs: drop the copy of the loadout it was given then, so it reads their real one.
		if (!HasAuthority() && NetLoadout)
		{
			NetLoadout = nullptr;
			if (HasActorBegunPlay()) Weapon->RebindLoadout();
		}
		Body->FollowLoadout(true);
		// If it was shown whole a moment ago (set up as someone else's first, as a joiner's own
		// character often is), the body and its grafts go back to shadow only for your own view.
		Body->SetShadowOnly(true);
	}
	else
	{
		Body->SetShadowOnly(false);
		if (!NetFlora.IsNone()) Body->SetModel(NetFlora);
		ShownLook.Reset();
		ShowLook(); // their grafts, if what they have on is known yet (else when it arrives: OnRep_NetLook)
	}
}

void AWWOCharacter::OnRep_NetFlora()
{
	if (!bLocalVisuals && !NetFlora.IsNone()) Body->SetModel(NetFlora);
}

void AWWOCharacter::OnRep_NetLook()
{
	// A joiner keeps a copy of everyone else's loadout (as the host does for joiners and bots), so
	// whatever belongs to that character reads what they have on, not what you do.
	if (IsLocalHuman()) return; // your own comes from your own loadout
	if (!HasAuthority())
	{
		if (!NetLoadout) NetLoadout = UWWOLoadoutSubsystem::NewCopy(UWWOLoadoutSubsystem::Get(GetWorld()));
		if (NetLoadout) NetLoadout->ApplyNet(NetLook);
	}
	if (HasActorBegunPlay() && !bLocalVisuals) ShowLook();
}

void AWWOCharacter::OnRep_NetFists()
{
	// Someone else's fists, as the host says (your own are lit from your own ability state).
	if (IsLocalHuman()) return;
	const UWWOLoadoutSubsystem* Data = UWWOLoadoutSubsystem::Get(GetWorld());
	const UWWOAbilityData* Lit = nullptr;
	if (Data && !NetFists.IsNone()) for (const UWWOAbilityData* A : Data->Catalog.Abilities) if (A && A->Id == NetFists) Lit = A;
	Body->SetHandGlow(Lit ? Lit->Color : FLinearColor::Black, Lit ? 0.85f : 0.f);
}

void AWWOCharacter::ShowLook()
{
	// Your own body is never shown to you from here: it follows your own loadout, shadow only
	// (a look that arrives late for your own character must not turn the whole body back on).
	if (bLocalVisuals) return;
	// Rebuilt only when something you can see changed (not for a swapped gun or its mods).
	FString Look = NetLook.Flora.ToString() + TEXT("|") + NetLook.Grenade.ToString();
	for (const FName& Id : NetLook.Abilities) Look += TEXT("|") + Id.ToString();
	for (const FName& Id : NetLook.FrameParts) Look += TEXT("|") + Id.ToString();
	if (NetLook.Flora.IsNone() || Look == ShownLook) return;
	ShownLook = Look;
	Body->FollowLoadout(false); // their flora's grafts and their shell's colors, from their loadout
	UE_LOG(LogWWO, Display, TEXT("Net visuals: %s shows %s"), *GetName(), *Look);
}

void AWWOCharacter::ServerSetFlora_Implementation(FName Flora)
{
	NetFlora = Flora;
	OnRep_NetFlora(); // the host shows it too
}

void AWWOCharacter::ServerSetLoadout_Implementation(const FWWONetLoadout& Loadout)
{
	if (!NetLoadout) NetLoadout = UWWOLoadoutSubsystem::NewCopy(UWWOLoadoutSubsystem::Get(GetWorld()));
	if (NetLoadout) NetLoadout->ApplyNet(Loadout);
	// Every machine gets it straight away (a joiner standing still sends no moves for the per-frame check to ride on).
	NetLook = Loadout;
	OnRep_NetLook();
	UE_LOG(LogWWO, Display, TEXT("Net: %s's loadout: flora %s, grenade %s, abilities %s, %d traits, weapon %s with %d mods"), *GetName(),
		*Loadout.Flora.ToString(), *Loadout.Grenade.ToString(), *FString::JoinBy(Loadout.Abilities, TEXT("+"), [](const FName& N) { return N.ToString(); }),
		Loadout.Traits.Num(), *Loadout.Weapon.ToString(), Loadout.ModIds.Num());
}

float AWWOCharacter::LocalRaise() const
{
	// How far this player has its gun up, 0-1: aimed in, or just fired from the hip.
	if (Health->bDead || bUlting || Weapon->bReloading || Weapon->bMeleeing || MoveState.bSprinting || MoveState.bSliding || MoveState.bMantling) return 0.f;
	return FMath::Max(Weapon->AdsAmount, Weapon->SecondsSinceShot() < 0.6f ? 0.7f : 0.f);
}

void AWWOCharacter::SendPose(float DeltaSeconds)
{
	// What others should see of what you're doing with your upper body: how far the gun is up and
	// where you're looking (up or down). Your own machine knows; a joiner tells the host ten times
	// a second, and the host's copy of you poses its body, which replicates to everyone else.
	PoseRaise = LocalRaise();
	PosePitch = FMath::RadiansToDegrees(Pitch);
	if (GetNetMode() != NM_Client || (PoseSendLeft -= DeltaSeconds) > 0.f) return;
	PoseSendLeft = 0.1f;
	const uint8 Raise = (uint8)FMath::RoundToInt(FMath::Clamp(PoseRaise, 0.f, 1.f) * 255.f);
	const int8 Look = (int8)FMath::Clamp(FMath::RoundToInt(PosePitch), -89, 89);
	if (Raise == SentRaise && Look == SentLook) return;
	SentRaise = Raise;
	SentLook = Look;
	ServerSetPose(Raise, Look);
}

void AWWOCharacter::ServerSetPose_Implementation(uint8 Raise, int8 Look)
{
	PoseRaise = Raise / 255.f;
	PosePitch = FMath::Clamp((float)Look, -89.f, 89.f);
}

void AWWOCharacter::RemoteFootsteps(float DeltaSeconds)
{
	// Someone else's steps, heard from where they are: paced by how fast they move (their own
	// machine paces its own by the head bob).
	const float Speed = GetVelocity().Size2D() / WWO::M;
	if (Health->bDead || bIsCrouched || Speed < 0.5f || !GetCharacterMovement()->IsMovingOnGround()) return;
	const bool bRunning = Speed > WalkSpeed * 1.3f;
	const float Before = RemoteStride;
	RemoteStride += Speed / FMath::Max(bRunning ? SprintStride : WalkStride, 0.1f) * DeltaSeconds;
	if (FMath::FloorToInt(RemoteStride) != FMath::FloorToInt(Before)) UWWOSound::PlayFor(this, "footstep", bRunning ? 1.f : 0.75f);
}

void AWWOCharacter::UpdateModel()
{
	// The upper body: arms up as far as the gun is raised (aimed in, or just fired), and the torso
	// leaning with the look, up or down. Eased here; the body replicates both to the other machines.
	{
		const float Step = GetWorld()->GetDeltaSeconds();
		const bool bPlain = Health->bDead || bUlting || EntrenchLeft > 0.f;
		// (The gun is carried up all the time now, not only while aimed in or firing: PoseRaise still
		// replicates, but the arms no longer wait for it.)
		// Sprinting it comes down: the run's own arms carry it (told from the clip, which every machine has).
		// (And crouched or sliding: those clips hold the gun themselves, and the aimed upper body over
		// a crouch folds the body forward.)
		const FName Now = Body->CurrentAnim();
		const bool bLowered = bPlain || Now == "Sprint" || Now == "Crouch_Idle" || Now == "Crouch_Fwd" || Now == "Slide" || Now == "Slide_Start";
		ShownRaise = WWO::MoveToward(ShownRaise, bLowered ? 0.f : 1.f, 7.f * Step);
		ShownPitch = FMath::Lerp(ShownPitch, bPlain ? 0.f : PosePitch, WWO::Damp(12.f, Step));
		// A hand cannon is held in the pistol stance, both hands on the grip; everything else in the rifle's.
		const UWWOLoadoutSubsystem* Loadout = UWWOLoadoutSubsystem::Get(this);
		const UWWOWeaponData* Held = Loadout ? Loadout->GetWeapon() : nullptr;
		Body->SetUpperBody(Held && Held->Id == "handcannon" ? FName("Aim_Pistol") : FName("Pistol_Aim_Neutral"), ShownRaise);
		Body->SetHunch(-ShownPitch * 0.55f, ShownPitch * 0.25f); // (0.55: UWWOWeaponComponent::UpdateWorldGun reads the look back from it)
	}
	// Pick the body animation from what you're doing. Playback speeds up or slows to match the
	// speed (m/s) each animation looks right at.
	constexpr float WalkAnimSpeed = 1.6f, JogAnimSpeed = 4.5f, SprintAnimSpeed = 7.f, CrouchAnimSpeed = 1.5f;
	const float Speed = GetCharacterMovement()->Velocity.Size2D() / WWO::M;
	if (Health->bDead)
	{
		// Down (online: until a teammate revives you). Once: replaying a finished clip starts it over.
		if (!bPosedDown) Body->Play("Death01", 1.f, 0.1f);
		bPosedDown = true;
		return;
	}
	bPosedDown = false;
	if (MoveState.bMantling)
	{
		Body->Play("Jump_Start");
	}
	else if (MoveState.bSliding)
	{
		// Drop into the slide (Slide_Start), then hold the sliding pose (Slide, looping).
		const FName Now = Body->CurrentAnim();
		if (Now != "Slide_Start" && Now != "Slide")
		{
			Body->Play("Slide_Start", 1.6f, 0.08f);
			Body->Queue("Slide");
		}
	}
	else if (!IsOnFloor()) Body->Play("Jump");
	else if (MoveState.bCrouching)
	{
		if (Speed > 0.3f) Body->Play("Crouch_Fwd", FMath::Clamp(Speed / CrouchAnimSpeed, 0.6f, 2.f));
		else Body->Play("Crouch_Idle");
	}
	else if (Speed < 0.3f) Body->Play("Idle");
	else if (MoveState.bSprinting) Body->Play("Sprint", FMath::Clamp(Speed / SprintAnimSpeed, 0.6f, 2.f));
	else if (Speed > 3.f) Body->Play("Jog_Fwd", FMath::Clamp(Speed / JogAnimSpeed, 0.6f, 2.f));
	else Body->Play("Walk", FMath::Clamp(Speed / WalkAnimSpeed, 0.6f, 2.f));
}
