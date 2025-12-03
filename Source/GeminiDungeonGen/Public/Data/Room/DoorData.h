// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "DoorData.generated.h"

class ADoorway;

UCLASS()
class GEMINIDUNGEONGEN_API UDoorData : public UDataAsset
{
	GENERATED_BODY()

public:
	// --- Door Frame Components (Static Geometry) ---
	
	// The wall module style for the door frame (e.g., side pillars, header piece).
	// This uses a custom struct to handle the various static meshes for the frame.
	// You may want to define a new specific struct like FDoorFrameModule if it differs greatly from FWallModule.
	// For simplicity, we'll use a version of FWallModule structure here for the frame meshes.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Frame Geometry")
	TSoftObjectPtr<UStaticMesh> FrameSideMesh;
	
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Frame Geometry")
	TSoftObjectPtr<UStaticMesh> FrameTopMesh;
	
	// The footprint (in 100cm units) this door frame occupies along the wall boundary.
	// Common door widths are 200cm (2 units).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Frame Geometry")
	int32 FrameFootprintY = 2; 

	// --- Functional Door Actor ---

	// The actual Blueprint Class of the Door Actor (e.g., an actor that handles opening/closing/replication)
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Door Functionality")
	TSubclassOf<ADoorway> DoorwayClass;

	// --- Connection Logic ---

	// The size/extent of the collision box used by the DungeonManager to detect door connection points.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Connection")
	FVector ConnectionBoxExtent = FVector(50.0f, 50.0f, 200.0f);
	
	// Placement weight for this door style (if multiple are available)
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Connection")
	float PlacementWeight = 1.0f;
};
