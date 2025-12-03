// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Data/Room/WallData.h"
#include "RoomData.generated.h"

class UFloorData;
class UWallData;
class UDoorData;
struct FMeshPlacementInfo;

UCLASS()
class GEMINIDUNGEONGEN_API URoomData : public UDataAsset
{
	GENERATED_BODY()

public:
	// --- General Dungeon Layout Parameters ---

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Layout")
	FIntPoint GridSize = FIntPoint(10, 10); // Room size in 100cm cells (e.g., 10x10 meters)

	// --- Style Data Asset References (Designer Swaps) ---

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Styles")
	TSoftObjectPtr<UFloorData> FloorStyleData;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Styles")
	TSoftObjectPtr<UWallData> WallStyleData;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Styles")
	TSoftObjectPtr<UDoorData> DoorStyleData;
	
	// --- Interior Mesh Randomization Pool ---

	// Meshes used to fill the interior of the room grid (clutter, furniture, etc.)
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Interior Meshes")
	TArray<FMeshPlacementInfo> InteriorMeshPool;
};
