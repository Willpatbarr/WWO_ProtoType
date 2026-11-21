#pragma once

// The character menu's shell part pictures: each part alone (no skeleton, no flora), three-quarters on, framed
// tightly on its bounds, in the player's colours, cut out of a clear background. Rendered live, not baked, so
// they follow the shell meshes and the colours as they are: a few capture stations far below the level (a
// part mesh and a scene capture each) take the pictures the menu asks for, a few at a time over the next
// frames, into small render targets that are kept and shared (one per part mesh and colours). The parts are on
// lighting channel 2, so the character preview's studio lights (AWWOPreviewActor) light them as they light the
// preview robot. Only the menu drives it (Step, from its tick while it is open), so nothing is captured in play.

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "WWOPartPictures.generated.h"

class USkeletalMesh;
class USkeletalMeshComponent;
class USceneCaptureComponent2D;
class UTextureRenderTarget2D;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class UWWOFramePartData;

// One picture: its target, the material that shows it, and what it shows.
USTRUCT()
struct FWWOPartPicture
{
	GENERATED_BODY()
	UPROPERTY() TObjectPtr<UTextureRenderTarget2D> Target;
	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> Mat;
	UPROPERTY() TObjectPtr<USkeletalMesh> Mesh;
	int32 Slot = 0;              // UWWOBodyComponent::EShellSlot
	FColor Paint[3] = {};        // primary, secondary, glow (alpha 0: the part's own)
	bool bTaken = false;
	uint64 Used = 0;             // when it was last asked for (the least recent go first when there are too many)
};

UCLASS()
class WHATWASOURS_API AWWOPartPictures : public AActor
{
	GENERATED_BODY()

public:
	AWWOPartPictures();
	virtual void BeginPlay() override;

	// The picture's shape (3:2) and its target's size in pixels (about twice what the Loadout tab shows at 1080p,
	// with mips for the Shell tab's smaller ones).
	static inline const FVector2D Shape = FVector2D(3.f, 2.f);
	static inline const FIntPoint Pixels = FIntPoint(432, 288);
	static constexpr int32 Stations = 4;

	// The picture of Part (none: the base robot's) in Slot (EShellSlot) in these colours: a UI material ready to
	// show now, which fills in when its capture is taken (over the next few Steps; it is clear until then).
	UMaterialInterface* Picture(const UWWOFramePartData* Part, int32 Slot, const FColor (&Paint)[3]);
	// Takes the waiting pictures, a few each frame. The menu calls it from its tick while it is open.
	void Step();
	int32 Waiting() const { return Queue.Num(); }

private:
	UPROPERTY() TArray<TObjectPtr<USkeletalMeshComponent>> Parts;
	UPROPERTY() TArray<TObjectPtr<USceneCaptureComponent2D>> Captures;
	UPROPERTY() TMap<FString, FWWOPartPicture> Cache;
	TArray<FString> Queue;              // keys still to take, first asked first
	FString Busy[Stations];             // the key each station is taking ("" idle)
	int32 Frames[Stations] = {};        // frames since the station was set up
	uint64 Clock = 0;
	// Frames a station waits after setting a part up before it captures (its render state is sent at the end of
	// the frame), and how many captures it takes (the last one stands).
	static constexpr int32 SettleFrames = 1, Shots = 2;
	static constexpr int32 MostKept = 40;
	void SetUp(int32 Station, const FWWOPartPicture& Pic);
	void Trim();
};
