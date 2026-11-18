#pragma once

// Why the host said no to a joiner's kit, as values, and the one place they become words.
// The host's checks (UWWOGrenadeComponent::HostThrow, UWWOAbilityComponent::HostUseAbility,
// AWWOCharacter::ServerActivateUlt) return an EWWOKitRefusal; AWWOCharacter::ClientKitRefused tells the
// joiner which kit (an EWWOKit, a uint8 on the wire) so it can put back what it started. No HUD shows a
// refusal today; the words are the host's NetThrow / NetAbility / NetUlt log lines.

#include "CoreMinimal.h"

// Which kit a refusal is about (ClientKitRefused's What).
enum class EWWOKit : uint8
{
	Grenade = 0,
	Ability = 1,
	Ult = 2,
};

enum class EWWOKitRefusal : uint8
{
	None,            // allowed
	Dead,            // its owner is dead
	EmptySlot,       // no ability in that slot
	NotReady,        // not charged (a little slack is allowed for timing)
	ThrownElsewhere, // a grenade thrown from somewhere the thrower isn't
};

namespace WWOKit
{
	// "the grenade", "the ability", "the ult"
	inline const TCHAR* Name(EWWOKit Kit)
	{
		switch (Kit)
		{
		case EWWOKit::Grenade: return TEXT("the grenade");
		case EWWOKit::Ability: return TEXT("the ability");
		default: return TEXT("the ult");
		}
	}

	// The words for a refusal of this kit (Charge: the host's charge, for a grenade that wasn't ready).
	inline FString RefusalText(EWWOKit Kit, EWWOKitRefusal Why, float Charge = 0.f)
	{
		switch (Why)
		{
		case EWWOKitRefusal::None: return Kit == EWWOKit::Ult ? TEXT("cast") : TEXT("used");
		case EWWOKitRefusal::Dead: return TEXT("dead");
		case EWWOKitRefusal::EmptySlot: return TEXT("empty");
		case EWWOKitRefusal::ThrownElsewhere: return TEXT("thrown from somewhere else");
		case EWWOKitRefusal::NotReady:
		default:
			if (Kit == EWWOKit::Grenade) return FString::Printf(TEXT("no grenade ready (charge %.2f)"), Charge);
			return Kit == EWWOKit::Ability ? TEXT("refused, not ready") : TEXT("refused");
		}
	}
}
