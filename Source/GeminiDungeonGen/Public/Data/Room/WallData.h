// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Data/Grid/GridData.h"
#include "WallData.generated.h"



UCLASS()
class GEMINIDUNGEONGEN_API UWallData : public UDataAsset
{
	GENERATED_BODY()

public:
	// The collection of all available wall modules (Base/Middle/Top components)
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Wall Modules")
	TArray<FWallModule> AvailableWallModules;

	// The default static mesh to use for the floor in the room (e.g., a simple square tile)
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Wall Defaults")
	TSoftObjectPtr<UStaticMesh> DefaultCornerMesh; 
	
	// Per-corner position offsets (clockwise from bottom-left)
	// Allows independent adjustment of each corner based on mesh pivot/size
	// Example: For 200x200 corner with center pivot, use offset (-100, -100, 0)
	
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Corner Position Adjustments")
	FVector SouthWestCornerOffset = FVector::ZeroVector;  // Corner at (0, 0)
	
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Corner Position Adjustments")
	FVector NorthWestCornerOffset = FVector::ZeroVector;  // Corner at (0, GridSize.Y)
	
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Corner Position Adjustments")
	FVector NorthEastCornerOffset = FVector::ZeroVector;  // Corner at (GridSize.X, GridSize.Y)
	
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Corner Position Adjustments")
	FVector SouthEastCornerOffset = FVector::ZeroVector;  // Corner at (GridSize.X, 0)
	
	// Wall position adjustments for fine-tuning alignment
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Position Adjustments")
	float NorthWallOffsetX = 0.0f;
	
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Position Adjustments")
	float SouthWallOffsetX = 0.0f;
	
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Position Adjustments")
	float EastWallOffsetY = 0.0f;
	
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Position Adjustments")
	float WestWallOffsetY = 0.0f;
	
	// Default height for the wall geometry, based on the middle mesh
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Wall Defaults")
	float WallHeight = 400.0f;
};