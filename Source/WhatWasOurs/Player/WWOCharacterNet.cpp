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
#include "Player/WWOTestTally.h"

using namespace WWOCharacter; // the constants these files share (WWOCharacterPrivate.h)

void AWWOCharacter::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AWWOCharacter, NetFlora);
	DOREPLIFETIME(AWWOCharacter, NetLook);
	DOREPLIFETIME(AWWOCharacter, NetFists);
}


void AWWOCharacter::ServerUseAbility_Implementation(int32 Slot, FVector_NetQuantizeNormal Aim)
{
	FWWOTestTally::Count(&FWWOTestTally::AbilitiesUsed);
	NetAim = FVector(Aim).GetSafeNormal(UE_SMALL_NUMBER, GetActorForwardVector()); // this copy's camera has no pitch
	if (Abilities->HostUseAbility(Slot) != EWWOKitRefusal::None) ClientKitRefused((uint8)EWWOKit::Ability, Slot);
}

void AWWOCharacter::ClientKitRefused_Implementation(uint8 What, int32 Slot)
{
	const EWWOKit Kit = (EWWOKit)What; // a uint8 on the wire
	UE_LOG(LogWWO, Display, TEXT("Net: the host refused %s"), WWOKit::Name(Kit));
	switch (Kit)
	{
	case EWWOKit::Grenade: Grenades->Charge = FMath::Min(Grenades->Charge + 1.f, Grenades->MaxCharges()); break;
	case EWWOKit::Ability: Abilities->UndoUse(Slot); break;
	default: Ult->Abort(true); break;
	}
}

void AWWOCharacter::ServerActivateUlt_Implementation()
{
	if (Ult->Charge >= 0.95f) Ult->Fill(); // a little slack: the two charges run a frame or two apart
	const bool bCast = Ult->ActivateUlt();
	if (!bCast) ClientKitRefused((uint8)EWWOKit::Ult, 0);
	FWWOTestTally::Count(&FWWOTestTally::UltsCast, bCast ? 1 : 0);
	UE_LOG(LogWWO, Display, TEXT("NetUlt from %s: %s"), *GetName(), *WWOKit::RefusalText(EWWOKit::Ult, bCast ? EWWOKitRefusal::None : EWWOKitRefusal::NotReady));
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
