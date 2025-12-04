// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Data/Grid/GridData.h"
#include "Data/Room/RoomData.h"
#include "MasterRoom.generated.h"

UCLASS()
class GEMINIDUNGEONGEN_API AMasterRoom : public AActor
{
	GENERATED_BODY()

public:
	AMasterRoom();

	// --- Generation Parameters ---

	// The Data Asset defining this room's layout and content rules
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Generation")
	URoomData* RoomData;

	// The seed used for generation (set by DungeonManager, tweakable by designer)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Replicated, Category = "Generation|Seed")
	int32 GenerationSeed = 1337;

	// --- EDITOR ONLY: Generate Button ---
	// Changing this boolean property triggers the RegenerateRoom function in the editor.
	UPROPERTY(EditAnywhere, Category = "Generation|Debug")
	bool bGenerateRoom = false; 

	// --- Designer Override Control ---

	// Array of rectangular regions the designer wants to force empty
	// Use this to create L-shapes, T-shapes, courtyards, or irregular room layouts
	// Each region is defined by two corner points (start and end)
	UPROPERTY(EditAnywhere, Category = "Generation|Designer Overrides|Floor")
	TArray<FForcedEmptyRegion> ForcedEmptyRegions;
	
	// Array of specific 100cm cell coordinates the designer wants to force empty
	// Use this for one-off cells or fine-tuning after regions are defined
	UPROPERTY(EditAnywhere, Category = "Generation|Designer Overrides|Floor")
	TArray<FIntPoint> ForcedEmptyFloorCells;
	
	// Array of specific meshes to force-place at coordinates (Hybrid System Control)
	UPROPERTY(EditAnywhere, Category = "Generation|Designer Overrides|Floor")
	TMap<FIntPoint, FMeshPlacementInfo> ForcedInteriorPlacements;

	// Array of fixed door locations on room boundaries
	// Doors are placed first, then walls fill gaps between them
	UPROPERTY(EditAnywhere, Category = "Generation|Designer Overrides|Walls")
	TArray<FFixedDoorLocation> FixedDoorLocations;

	// --- Door Position Offset Controls ---
	// Global offset applied to all doors for fine-tuning alignment with floor edges
	// Note: Wall offsets are now in WallData asset (per-wall-type configuration)
	
	UPROPERTY(EditAnywhere, Category = "Generation|Designer Overrides|Doors|Position Offsets")
	FVector DoorPositionOffset = FVector::ZeroVector;
	
	// --- Procedural Door Placement ---
	
	// Enable automatic placement of ONE door per edge (in valid gaps)
	// Doors are size-appropriate and placed randomly in available space
	// Designers can manually override by adding to FixedDoorLocations
	UPROPERTY(EditAnywhere, Category = "Generation|Procedural Doors")
	bool bEnableProceduralDoors = false;

private:
	// Internal grid array to track occupancy (used during runtime generation)
	TArray<EGridCellType> InternalGridState;
	
	// Occupancy grid for tracking what's placed in each cell (walls, doors, etc.)
	// Used during generation to prevent overlapping placement
	TMap<FIntPoint, EGridCellType> OccupancyGrid;
	
	// Flag to prevent infinite recursion during procedural door placement
	bool bIsPlacingProceduralDoors = false;
	
	// Map to hold and manage HISM components (one HISM per unique Static Mesh)
	TMap<UStaticMesh*, UHierarchicalInstancedStaticMeshComponent*> MeshToHISMMap;
	
protected:
	virtual void PostLoad() override;
	virtual void GetLifetimeReplicatedProps(TArray<class FLifetimeProperty>& OutLifetimeProps) const override;
	
	// Override used to monitor changes in the Details Panel (for the bGenerateRoom button trick)
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	
	// --- Core Generation Functions ---

	// Selects one FMeshPlacementInfo struct based on placement weights
	const FMeshPlacementInfo* SelectWeightedMesh(const TArray<FMeshPlacementInfo>& MeshPool, FRandomStream& Stream);
	
	// Expands all ForcedEmptyRegions into individual cell coordinates
	// Combines with ForcedEmptyFloorCells and returns a complete list
	TArray<FIntPoint> ExpandForcedEmptyRegions() const;
	
	// UFUNCTION to be called by the DungeonManager (or designer in editor)
	UFUNCTION(BlueprintCallable, CallInEditor, Category = "Generation")
	void RegenerateRoom();
	
	// Logic for clearing and resetting all HISM components
	void ClearAndResetComponents();
	
	// Logic for getting or creating the HISM component for a given mesh
	UHierarchicalInstancedStaticMeshComponent* GetOrCreateHISM(UStaticMesh* Mesh);
	
	// Core grid packing logic (for floor and interior meshes)
	void GenerateFloorAndInterior();
	
	// 1D wall placement logic using WallDataAsset
	void GenerateWallsAndDoors();

	void ExecuteForcedPlacements(FRandomStream& Stream);
	
	// Helper function for drawing the debug grid in the editor
	void DrawDebugGrid();

	// --- Wall Generation Helper Functions ---
	
	// Get all cell coordinates for a specific wall edge
	TArray<FIntPoint> GetCellsForEdge(EWallEdge Edge) const;
	
	// Get the rotation for walls on a specific edge (all face inward)
	FRotator GetWallRotationForEdge(EWallEdge Edge) const;
	
	// Calculate world position for a wall module on North/South edges
	FVector CalculateNorthSouthWallPosition(int32 X, int32 StartY, float WallMeshLength, bool bIsNorthWall) const;
	
	// Calculate world position for a wall module on East/West edges
	FVector CalculateEastWestWallPosition(int32 StartX, int32 Y, float WallMeshLength, bool bIsEastWall) const;
	
	// Calculate world position for a door (independent of wall positioning)
	// Doors snap to floor edges using interior cells, not boundary cells
	FVector CalculateDoorPosition(EWallEdge Edge, int32 StartCell, float DoorWidth) const;
	
	// Fill a wall segment with wall modules using bin packing
	void FillWallSegment(EWallEdge Edge, int32 SegmentStart, int32 SegmentLength, FRandomStream& Stream);
	
	// --- Procedural Door Placement ---
	
	// Automatically place doors in valid gaps using DoorStylePool
	// Called after fixed doors are processed if bEnableProceduralDoors is true
	void PlaceProceduralDoors(FRandomStream& Stream);
	
	// --- Door Variety Helper Functions (Hybrid System) ---
	
	// Select a random door from the RoomData's DoorStylePool using weighted selection
	// Returns nullptr if pool is empty
	UDoorData* SelectRandomDoorFromPool(FRandomStream& Stream) const;
	
	// Check if a door of given footprint can fit at the specified location
	// Returns true if there's enough space and no overlap with existing doors
	bool CanFitDoor(EWallEdge Edge, int32 StartCell, int32 Footprint) const;
	
	// Get the number of available consecutive cells on an edge (between existing doors)
	// Useful for finding valid placement locations for procedural doors
	int32 GetAvailableSpaceOnEdge(EWallEdge Edge, int32 StartCell) const;
	
	// Get all valid door placement locations on an edge (gaps between fixed doors)
	// Returns array of {StartCell, MaxFootprint} pairs
	TArray<TPair<int32, int32>> GetValidDoorLocations(EWallEdge Edge) const;
};