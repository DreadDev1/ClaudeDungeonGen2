// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "CeilingData.generated.h"

// Struct for ceiling tile definitions (supports different sizes for efficient coverage)
USTRUCT(BlueprintType)
struct FCeilingTile
{
	GENERATED_BODY()

	// The static mesh for this ceiling tile
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ceiling Tile")
	TSoftObjectPtr<UStaticMesh> Mesh;

	// Size of this tile in grid cells (1 = 100x100, 4 = 400x400)
	// 400x400 tiles cover 4x4 grid cells (16 cells total)
	// 100x100 tiles cover 1x1 grid cell (1 cell)
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ceiling Tile")
	int32 TileSize = 1;

	// Placement weight for weighted random selection (0.0 to 10.0)
	// Higher weight = more likely to be selected
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ceiling Tile", meta=(ClampMin="0.0", ClampMax="10.0"))
	float PlacementWeight = 1.0f;
};

UCLASS()
class GEMINIDUNGEONGEN_API UCeilingData : public UDataAsset
{
	GENERATED_BODY()

public:
	// Large ceiling tiles (400x400) - used to fill majority of ceiling
	// These are placed first to cover large areas efficiently
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ceiling Tiles")
	TArray<FCeilingTile> LargeTilePool;

	// Small ceiling tiles (100x100) - used to fill gaps and edges
	// These are placed second to complete coverage
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ceiling Tiles")
	TArray<FCeilingTile> SmallTilePool;

	// Height of the ceiling above the floor (Z offset)
	// Default 500cm matches 5m tall walls
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ceiling Settings")
	float CeilingHeight = 500.0f;

	// Rotation offset for all ceiling tiles
	// Use (0, 180, 0) to flip floor tiles upside down for ceiling
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ceiling Settings")
	FRotator CeilingRotation = FRotator(0.0f, 180.0f, 0.0f);
};