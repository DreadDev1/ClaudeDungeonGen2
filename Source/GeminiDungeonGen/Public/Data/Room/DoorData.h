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
		
	// The footprint (in cell count) this door frame occupies along the wall boundary.
	// Examples: 2 = standard door (2 cells = 200cm), 4 = double door (400cm), 8 = hangar door (800cm)
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Frame Geometry")
	int32 FrameFootprintY = 2;
	
	// NEW: Designer-editable rotation offset to correct for mesh import orientation issues.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Frame Geometry")
	FRotator FrameRotationOffset = FRotator::ZeroRotator;
	
	// --- Door Variety Pool (Hybrid System) ---
	
	// Pool of door styles available in this door pack
	// Each entry represents a different door variant (size, style, etc.)
	// Used for procedural door placement with weighted random selection
	// NOTE: Leave empty to use only the single door properties above (backwards compatible)
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Door Varieties")
	TArray<UDoorData*> DoorStylePool;
	
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