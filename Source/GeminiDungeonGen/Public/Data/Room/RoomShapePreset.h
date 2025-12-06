// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Data/Grid/GridData.h"
#include "RoomShapePreset.generated.h"

// Enum for common room shapes (for designer clarity)
UENUM(BlueprintType)
enum class ERoomShapeType : uint8
{
	Rectangular		UMETA(DisplayName = "Rectangular (Standard)"),
	L_Shape			UMETA(DisplayName = "L-Shape"),
	T_Shape			UMETA(DisplayName = "T-Shape"),
	Plus_Shape		UMETA(DisplayName = "Plus Shape (+)"),
	U_Shape			UMETA(DisplayName = "U-Shape"),
	Triangle		UMETA(DisplayName = "Triangle"),
	Diamond			UMETA(DisplayName = "Diamond"),
	Hexagon			UMETA(DisplayName = "Hexagon"),
	Octagon			UMETA(DisplayName = "Octagon"),
	Custom			UMETA(DisplayName = "Custom Shape")
};

/**
 * Room Shape Preset - Defines a pre-configured room shape
 * 
 * This data asset allows designers to create reusable room shapes by defining
 * which cells should be empty. The shape is applied to MasterRoom on generation.
 * 
 * WORKFLOW:
 * 1. Create RoomShapePreset asset
 * 2. Set ShapeType (L-Shape, T-Shape, etc.)
 * 3. Define EmptyRegions to carve out the shape
 * 4. Assign to MasterRoom's ShapePreset property
 * 5. Generate room - shape is automatically applied!
 */
UCLASS(BlueprintType)
class GEMINIDUNGEONGEN_API URoomShapePreset : public UDataAsset
{
	GENERATED_BODY()

public:
	// The type of shape this preset creates (for designer reference)
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shape Info")
	ERoomShapeType ShapeType = ERoomShapeType::Rectangular;

	// Descriptive name for this shape preset
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shape Info")
	FString ShapeName = TEXT("Rectangular Room");

	// Description of this shape (helps designers understand what it creates)
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shape Info", meta=(MultiLine=true))
	FString ShapeDescription = TEXT("Standard rectangular room with no empty regions.");

	// Recommended minimum grid size for this shape
	// Helps designers know what room size works best
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shape Info")
	FIntPoint RecommendedMinSize = FIntPoint(10, 10);

	// ========================================================================
	// SHAPE DEFINITION
	// ========================================================================

	// Rectangular regions to force empty (creates the shape)
	// These are the areas that will be "carved out" of the room
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shape Definition")
	TArray<FForcedEmptyRegion> EmptyRegions;

	// Individual cells to force empty (for fine-tuning the shape)
	// Use this to round corners, add detail, or perfect the shape
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shape Definition")
	TArray<FIntPoint> EmptyCells;

	// ========================================================================
	// VISUAL PREVIEW INFO (optional - for future editor visualization)
	// ========================================================================

	// Preview thumbnail for this shape (optional)
	// Helps designers visually identify shapes in editor
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Visual")
	TSoftObjectPtr<UTexture2D> PreviewThumbnail;
};