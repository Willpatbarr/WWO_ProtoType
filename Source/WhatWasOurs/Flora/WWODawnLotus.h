#pragma once

// DawnLotus, the healing flora. Everything it does that isn't a table entry is here:
// - UWWODawnLotusTuning: EVERY DawnLotus number, in one place (Settings > DawnLotus binds to it by name).
// - The host's healing path (WWODawnLotus::HealTeammate), which reports what was really healed and what
//   was overheal, and feeds Nectar and Pollination from that.
// - UWWODawnLotusComponent: on every player, what DawnLotus remembers (Nectar, Petalfall's count, Pollination's
//   per-teammate clock, Reveille's handling boost) and the host's messages to a joiner.
// - AWWOBloomPool: the healing grenade's pool.
// The host decides all of it. A joiner's charges and magazine are its own to predict, so the host
// applies energy to its copy and tells the joiner the same amount.

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameFramework/Actor.h"
#include "Flora/WWOEffectLife.h"
#include "Engine/NetSerialization.h"
#include "WWODawnLotus.generated.h"

class AWWOCharacter;
class AWWOParticles;
class UMaterialInstanceDynamic;
class UPointLightComponent;
class UStaticMeshComponent;
class UWWOAbilityData;
class UWWOGrenadeData;

// Every DawnLotus number. The class default object is the live copy: one set for the whole game, so a
// joiner's copy on the host reads the host's values. Property names are saved-settings keys.
UCLASS()
class WHATWASOURS_API UWWODawnLotusTuning : public UObject
{
	GENERATED_BODY()

public:
	// Pollination (self, triggered): healing a teammate gives a little energy.
	UPROPERTY(EditAnywhere, Category = "Pollination") float PollinationGrenadeEnergy = 0.1f; // of one grenade charge
	UPROPERTY(EditAnywhere, Category = "Pollination") float PollinationBloomEnergy = 0.05f;  // of Full Bloom's charge
	UPROPERTY(EditAnywhere, Category = "Pollination") float PollinationCooldown = 4.f;       // s, per teammate

	// Full Bloom (teammates, activated): a fixed pool split evenly among teammates in the circle.
	UPROPERTY(EditAnywhere, Category = "Full Bloom") float FullBloomPool = 200.f;
	UPROPERTY(EditAnywhere, Category = "Full Bloom") float FullBloomRadius = 8.f;    // m
	UPROPERTY(EditAnywhere, Category = "Full Bloom") float FullBloomCooldown = 45.f; // s

	// Petalfall (teammates, triggered): every so much damage dealt releases a healing burst.
	UPROPERTY(EditAnywhere, Category = "Petalfall") float PetalfallDamage = 300.f; // damage dealt per burst
	UPROPERTY(EditAnywhere, Category = "Petalfall") float PetalfallHeal = 30.f;    // to each teammate caught
	UPROPERTY(EditAnywhere, Category = "Petalfall") float PetalfallRadius = 6.f;   // m

	// Bloom Pool (grenade): a pool that heals teammates standing in it.
	UPROPERTY(EditAnywhere, Category = "Bloom Pool") float BloomPoolHealPerSecond = 25.f;
	UPROPERTY(EditAnywhere, Category = "Bloom Pool") float BloomPoolTime = 6.f;         // s
	UPROPERTY(EditAnywhere, Category = "Bloom Pool") float BloomPoolRadius = 4.f;       // m
	UPROPERTY(EditAnywhere, Category = "Bloom Pool") float BloomPoolRecharge = 1.25f;   // x the global grenade recharge

	// Solar Burst (grenade): plain damage.
	UPROPERTY(EditAnywhere, Category = "Solar Burst") float SolarBurstDamage = 140.f;
	UPROPERTY(EditAnywhere, Category = "Solar Burst") float SolarBurstRadius = 4.5f;   // m
	UPROPERTY(EditAnywhere, Category = "Solar Burst") float SolarBurstRecharge = 1.f;  // x the global grenade recharge

	// Reveille (grenade): one blast; teammates caught, thrower included, get energy, handling and a full magazine.
	UPROPERTY(EditAnywhere, Category = "Reveille") float ReveilleDamage = 25.f;        // to enemies
	UPROPERTY(EditAnywhere, Category = "Reveille") float ReveilleRadius = 5.f;         // m
	UPROPERTY(EditAnywhere, Category = "Reveille") float ReveilleEnergy = 0.1f;        // of each ability's charge (never the grenade's)
	UPROPERTY(EditAnywhere, Category = "Reveille") float ReveilleHandling = 1.35f;     // reload, aim-in, swap and ready this much faster
	UPROPERTY(EditAnywhere, Category = "Reveille") float ReveilleHandlingTime = 8.f;   // s
	UPROPERTY(EditAnywhere, Category = "Reveille") float ReveilleRecharge = 1.5f;      // x the global grenade recharge

	// Nectar: filled by healing teammates, capped, never decays; the ult spends all of it.
	UPROPERTY(EditAnywhere, Category = "Nectar") float NectarMax = 100.f;
	UPROPERTY(EditAnywhere, Category = "Nectar") float NectarPerHeal = 0.25f;      // per point really healed
	UPROPERTY(EditAnywhere, Category = "Nectar") float NectarOverhealScale = 0.25f; // an overhealed point counts this much

	// The ult (placeholder form: one forward beam, first thing hit).
	UPROPERTY(EditAnywhere, Category = "Ult") float BeamBaseDamage = 300.f;
	UPROPERTY(EditAnywhere, Category = "Ult") float BeamDamageAtFullNectar = 450.f; // extra damage at full Nectar
	UPROPERTY(EditAnywhere, Category = "Ult") float BeamRange = 60.f;               // m
	UPROPERTY(EditAnywhere, Category = "Ult") float BeamRadius = 0.6f;              // m: how far off the line still counts as hit
	UPROPERTY(EditAnywhere, Category = "Ult") float BeamWindup = 0.65f;             // s of charging before it fires

	// Bloom Pool and Reveille go off on walkable ground; this long in the air and they go off anyway.
	UPROPERTY(EditAnywhere, Category = "Grenades") float GroundGrenadeSafetyFuse = 5.f; // s

	// How long a triggered ability's HUD box stays lit after it goes off.
	UPROPERTY(EditAnywhere, Category = "HUD") float DawnLitTime = 0.6f; // s
};

// What a heal really did.
struct FWWOHealResult
{
	float Actual = 0.f;   // health + shield restored
	float Overheal = 0.f; // the part of the heal that had nothing to fill
};

enum class EWWOHealSource : uint8
{
	FullBloom, // gives Nectar but never Pollination energy
	Petalfall,
	BloomPool,
};

UCLASS(ClassGroup = (WWO))
class WHATWASOURS_API UWWODawnLotusComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UWWODawnLotusComponent();
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	// Replicated to the owner only (its HUD).
	UPROPERTY(Replicated) float Nectar = 0.f;
	UPROPERTY(Replicated) float UltBonus = 0.f; // the extra damage the ult will do now, worked out by the host

	// Host: this player healed Mate (Nectar, Pollination).
	void OnHealed(AActor* Mate, const FWWOHealResult& Heal, EWWOHealSource Source);
	// Host: this player dealt damage (Petalfall).
	void OnDamageDealt(float Amount);
	// Host: the ult spends it all and returns the damage it adds.
	float SpendNectar();
	void SetNectar(float Value);

	// Host, on the player caught by a Reveille: here, and on that player's own machine.
	void GiveReveille(float AbilityEnergy, float HandlingSeconds);

	bool IsHandlingUp() const;
	float HandlingLeft() const;
	void ClearBoost() { HandlingUntil = 0.0; }

	// What the HUD shows, on this machine (world times; the HUD only reads them).
	double HealMarkAt = -100.0;   // your heal landed on a teammate
	float HealMarkAmount = 0.f;   // how much, added up while they keep landing
	double PetalFlashAt = -100.0; // your Petalfall went off
	double HealedAt = -100.0;     // a teammate healed you
	void NoteHealLanded(float Amount); // host: here, or on the healer's own machine
	void ShowHealPulse(AActor* Mate) { MulticastHealPulse(Mate); }
	void ShowPetalBurst(const FVector& At, float RadiusM, const FLinearColor& Color) { MulticastPetalBurst(At, RadiusM, Color); }
	void ShowBeam(const FVector& From, const FVector& To, const FLinearColor& Color, bool bHit) { MulticastBeam(From, To, Color, bHit); }

private:
	void GiveEnergy(float GrenadeEnergy, float BloomEnergy);
	void ApplyEnergy(float GrenadeEnergy, float BloomEnergy);
	void ApplyReveille(float AbilityEnergy, float HandlingSeconds);
	void Light(FName AbilityId); // its HUD box, here and on the owner's machine
	AWWOCharacter* Me() const;

	UFUNCTION(Client, Reliable) void ClientEnergy(float GrenadeEnergy, float BloomEnergy);
	UFUNCTION(Client, Reliable) void ClientReveille(float AbilityEnergy, float HandlingSeconds);
	UFUNCTION(Client, Reliable) void ClientLight(FName AbilityId);
	UFUNCTION(Client, Reliable) void ClientHealLanded(float Amount);
	UFUNCTION(NetMulticast, Unreliable) void MulticastHealPulse(AActor* Mate);
	UFUNCTION(NetMulticast, Reliable) void MulticastPetalBurst(FVector_NetQuantize At, float RadiusM, FLinearColor Color);
	UFUNCTION(NetMulticast, Reliable) void MulticastBeam(FVector_NetQuantize From, FVector_NetQuantize To, FLinearColor Color, bool bHit);
	void ApplyHealLanded(float Amount);
	void MarkLit(FName AbilityId);

	float PetalDamage = 0.f;      // host: damage dealt toward the next Petalfall
	double HandlingUntil = 0.0;   // this machine's world time
	TMap<TWeakObjectPtr<AActor>, double> PollinatedAt; // host: when each teammate last gave energy

	friend class FWWORegress;
};

// The Bloom Pool grenade's pool. Looks on every machine, healing on the host.
UCLASS()
class WHATWASOURS_API AWWOBloomPool : public AActor
{
	GENERATED_BODY()

public:
	AWWOBloomPool();
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	static AWWOBloomPool* Spawn(UWorld* World, const FVector& At, const FLinearColor& Color, AActor* Thrower);

private:
	UPROPERTY() TObjectPtr<UStaticMeshComponent> Disc;
	UPROPERTY() TObjectPtr<UStaticMeshComponent> Haze;
	UPROPERTY() TObjectPtr<UPointLightComponent> Glow;
	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> Mat;
	TWeakObjectPtr<AWWOParticles> Motes;
	TWeakObjectPtr<AActor> Thrower;
	UPROPERTY(Replicated) float Radius = 4.f;
	UPROPERTY(Replicated) float Duration = 6.f;
	UPROPERTY(Replicated) FLinearColor Color = FLinearColor::White;
	FWWOEffectLife Life;
	float TickLeft = 0.f;
};

namespace WWODawnLotus
{
	UWWODawnLotusTuning& T(); // the numbers

	// Host: heals a teammate (a player or an AI teammate; never yourself or the other team) and credits
	// Healer. Returns what it really did.
	FWWOHealResult HealTeammate(AWWOCharacter& Healer, AActor* Mate, float Amount, EWWOHealSource Source);

	void FullBloom(AWWOCharacter& P);                                  // host
	void PetalfallBurst(AWWOCharacter& P);                             // host
	void ReveilleBurst(AWWOCharacter& Thrower, const FVector& Center, float RadiusM); // host
	void FireBeam(AWWOCharacter& P, const FVector& Aim, const FVector& Muzzle, const FLinearColor& Color); // host: the ult

	// A DawnLotus item's numbers come from the tuning; every other item's from its asset.
	float GrenadeDamage(const UWWOGrenadeData& G);
	float GrenadeRadius(const UWWOGrenadeData& G);
	float GrenadeRecharge(const UWWOGrenadeData& G);
	float AbilityCooldown(const UWWOAbilityData& A);

	// A stat with Reveille's handling boost on it (the stats that are times get shorter).
	float WithHandling(FName Stat, float Value);
}
