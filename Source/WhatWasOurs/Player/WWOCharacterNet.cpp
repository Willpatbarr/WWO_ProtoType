// AWWOCharacter: what replicates as properties, the online kit RPCs (a joiner's ability and ult, the
// host's refusals) and holding Revive online. The other RPCs sit with what they serve: gunfire and
// melee in WWOCharacterGunNet.cpp, movement in WWOCharacterMove.cpp, status effects in
// WWOCharacterCombat.cpp, the look and pose in WWOCharacterLook.cpp.

#include "Player/WWOCharacter.h"
#include "WhatWasOurs.h"
#include "Player/WWOCharacterPrivate.h"
#include "WWOTypes.h"
#include "Engine/World.h"
#include "Net/UnrealNetwork.h"
#include "Player/WWOHealthComponent.h"
#include "Player/WWOAbilityComponent.h"
#include "Player/WWOGrenadeComponent.h"
#include "Player/WWOUltComponent.h"

using namespace WWOCharacter; // the constants these files share (WWOCharacterPrivate.h)

void AWWOCharacter::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AWWOCharacter, NetFlora);
	DOREPLIFETIME(AWWOCharacter, NetLook);
	DOREPLIFETIME(AWWOCharacter, NetFists);
}

int32 AWWOCharacter::NetAbilitiesUsed = 0;
int32 AWWOCharacter::NetUltsCast = 0;

void AWWOCharacter::ServerUseAbility_Implementation(int32 Slot, FVector_NetQuantizeNormal Aim)
{
	++NetAbilitiesUsed;
	NetAim = FVector(Aim).GetSafeNormal(UE_SMALL_NUMBER, GetActorForwardVector()); // this copy's camera has no pitch
	if (!Abilities->HostUseAbility(Slot)) ClientKitRefused(1, Slot);
}

void AWWOCharacter::ClientKitRefused_Implementation(uint8 What, int32 Slot)
{
	UE_LOG(LogWWO, Display, TEXT("Net: the host refused %s"), What == 0 ? TEXT("the grenade") : (What == 1 ? TEXT("the ability") : TEXT("the ult")));
	if (What == 0) Grenades->Charge = FMath::Min(Grenades->Charge + 1.f, Grenades->MaxCharges());
	else if (What == 1) Abilities->UndoUse(Slot);
	else Ult->Abort(true);
}

void AWWOCharacter::ServerActivateUlt_Implementation()
{
	if (Ult->Charge >= 0.95f) Ult->Fill(); // a little slack: the two charges run a frame or two apart
	const bool bCast = Ult->ActivateUlt();
	if (!bCast) ClientKitRefused(2, 0);
	NetUltsCast += bCast ? 1 : 0;
	UE_LOG(LogWWO, Display, TEXT("NetUlt from %s: %s"), *GetName(), bCast ? TEXT("cast") : TEXT("refused"));
}

void AWWOCharacter::ServerSetRevive_Implementation(bool bHolding)
{
	Health->bWantsRevive = bHolding;
}

void AWWOCharacter::UpdateReviveInput()
{
	if (GetNetMode() == NM_Standalone) return;
	const bool bHolding = !Health->bDead && Pressed(EWWOAction::Revive) && Health->FindReviveTarget();
	if (HasAuthority()) Health->bWantsRevive = bHolding;
	else if (bHolding != bSentRevive)
	{
		bSentRevive = bHolding;
		ServerSetRevive(bHolding);
	}
}
