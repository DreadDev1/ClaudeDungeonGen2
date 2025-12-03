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
	
	// Default height for the wall geometry, based on the middle mesh
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Wall Defaults")
	float WallHeight = 400.0f;
};
