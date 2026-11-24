#pragma once

// The character menu's shell part pictures: each part alone (no skeleton, no flora), three-quarters on, framed
// tightly on what shows of it, in the player's colours, cut out of a clear background. Rendered live, not baked,
// so they follow the shell meshes and the colours as they are: a few capture stations far below the level (a
// part mesh and two scene captures each) take the pictures the menu asks for, a few at a time over the next
// frames, into small render targets that are kept and shared (one pair per part mesh and colours).
// - Lit and toned as the character preview is: the parts are on lighting channel 2, so the preview's studio
//   lights (AWWOPreviewActor) light them, and the colour is a tone mapped capture with the preview's exposure and
//   bloom. A second capture, scene colour, gives the cut-out (its alpha); M_WWO_PartPicture puts the two together
//   (Tools/create_part_picture_material.py).
// - Framing: a small first capture of the cut-out (framed on the mesh's bounds) is read back off the GPU without
//   waiting for it, and the picture is framed on the rows and columns where the part really shows. A row or
//   column with only a thin protrusion in it (the head's antenna) does not count, so that crops out instead of
//   shrinking the part.
// Only the menu drives it (Step, from its tick while it is open), so nothing is captured in play.

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
struct FWWOPartProbe;

// One picture: its two targets, the material that shows them, and what it shows.
USTRUCT()
struct FWWOPartPicture
{
	GENERATED_BODY()
	UPROPERTY() TObjectPtr<UTextureRenderTarget2D> Color; // the tone mapped capture
	UPROPERTY() TObjectPtr<UTextureRenderTarget2D> Cut;   // scene colour: its alpha is 0 where the part is
	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> Mat;
	UPROPERTY() TObjectPtr<USkeletalMesh> Mesh;
	int32 Slot = 0;              // UWWOBodyComponent::EShellSlot
	FColor Paint[3] = {};        // primary, secondary, glow (alpha 0: the part's own)
	FColor Frame = FColor(0, 0, 0, 0); // the skeleton's frame colour, on the part's own frame and joint pieces
	bool bSmall = false;         // the Shell tab's size
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

	// The picture's shape (3:2) and its targets' size in pixels: about twice what the Loadout tab shows at 1080p, or
	// (Small) what the Shell tab's cards do. No mips (a scene capture's own were found blank on the Mac): each size
	// is drawn at about half its pixels. The framing probe's.
	static inline const FVector2D Shape = FVector2D(3.f, 2.f);
	static inline const FIntPoint Pixels = FIntPoint(336, 224);
	static inline const FIntPoint SmallPixels = FIntPoint(132, 88);
	static inline const FIntPoint ProbePixels = FIntPoint(216, 144);
	static constexpr int32 Stations = 4;

	// The picture of Part (none: the base robot's) in Slot (EShellSlot) in these colours: a UI material ready to
	// show now, which fills in when its capture is taken (over the next few Steps; it is clear until then).
	UMaterialInterface* Picture(const UWWOFramePartData* Part, int32 Slot, const FColor (&Paint)[3], FColor Frame = FColor(0, 0, 0, 0), bool bSmall = false);
	// Takes the waiting pictures, a few each frame. The menu calls it from its tick while it is open.
	void Step();
	int32 Waiting() const { return Queue.Num(); }

	// Where the part shows in a probe of the cut-out (Covered: the part is at that pixel): the rectangle (pixels,
	// Max exclusive) of the rows and columns holding at least MinShare as many part pixels as the fullest row or
	// column. Invalid if no part shows.
	static FBox2D ShownRect(TFunctionRef<bool(int32 X, int32 Y)> Covered, FIntPoint Size, float MinShare = 0.1f);

private:
	UPROPERTY() TArray<TObjectPtr<USkeletalMeshComponent>> Parts;
	UPROPERTY() TArray<TObjectPtr<USceneCaptureComponent2D>> ColorCaptures; // tone mapped, as the preview
	UPROPERTY() TArray<TObjectPtr<USceneCaptureComponent2D>> CutCaptures;   // scene colour: the cut-out, and the probe
	UPROPERTY() TArray<TObjectPtr<UTextureRenderTarget2D>> Probes;
	UPROPERTY() TMap<FString, FWWOPartPicture> Cache;
	TArray<FString> Queue;              // keys still to take, first asked first
	// Each station's job: the key it is taking ("" idle), its stage, frames in that stage, where its camera looks
	// and from how far, and the probe being read back.
	enum class EStage : uint8 { Idle, Settle, Probe, Shoot };
	struct FStation
	{
		FString Key;
		EStage Stage = EStage::Idle;
		int32 Frames = 0;
		FVector Middle = FVector::ZeroVector;
		float Distance = 0.f;
		TSharedPtr<FWWOPartProbe, ESPMode::ThreadSafe> Probe;
	};
	FStation Jobs[Stations];
	uint64 Clock = 0;
	// Frames a station waits after setting a part up before it captures (its render state is sent at the end of
	// the frame), how long it waits for the probe before keeping the bounds' framing, and how many captures of the
	// picture it takes (the last one stands).
	static constexpr int32 SettleFrames = 1;
	static constexpr int32 ProbeWait = 12;
	static constexpr int32 Shots = 2;
	static constexpr int32 MostKept = 40;
	void SetUp(int32 Station, const FWWOPartPicture& Pic);
	void Aim(int32 Station, const FVector& Middle, float Distance, float FieldOfView);
	void Reframe(int32 Station, const FBox2D& Rect);
	void Finish(int32 Station);
	void Trim();
};
