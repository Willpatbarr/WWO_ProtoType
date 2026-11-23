// A field of map dressing: one static mesh drawn many times (rocks, plants, ruin blocks) as one
// hierarchical instanced component, so a thousand ferns cost a few draw calls, each cluster is
// culled and picks its LOD on its own, and nothing ticks. Placed and filled by the map build
// scripts (Tools/cascade_dress.py); no gameplay: no collision, out of the navmesh.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "WWOPropField.generated.h"

class UHierarchicalInstancedStaticMeshComponent;

UCLASS()
class WHATWASOURS_API AWWOPropField : public AActor
{
	GENERATED_BODY()

public:
	AWWOPropField();

	// The instances. The build script sets its mesh, materials, shadow and cull distances, then adds transforms.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "WWO")
	TObjectPtr<UHierarchicalInstancedStaticMeshComponent> Instances;
};
