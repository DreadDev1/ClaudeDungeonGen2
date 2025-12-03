// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "FloorData.generated.h"

struct FMeshPlacementInfo;

UCLASS()
class GEMINIDUNGEONGEN_API UFloorData : public UDataAsset
{
	GENERATED_BODY()

public:
	// --- Base Floor Tiles ---
	
	// A collection of floor tiles/meshes used to fill the grid (e.g., 100x100, 200x200 tiles)
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Floor Tiles")
	TArray<FMeshPlacementInfo> FloorTilePool;

	// --- Floor Clutter / Detail Meshes ---
	
	// A separate pool for smaller details or clutter placed randomly on top of the main tiles.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Floor Clutter")
	TArray<FMeshPlacementInfo> ClutterMeshPool;

	// The likelihood (0.0 to 1.0) of attempting to place a clutter mesh in an empty floor cell.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Floor Clutter")
	float ClutterPlacementChance = 0.25f;

	// --- GAP FILLER TILE (NEW) ---
	// A specific 1x1 mesh used to fill any remaining empty cells after the main randomized pass.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Floor Tiles")
	TSoftObjectPtr<UStaticMesh> DefaultFillerTile;
};
