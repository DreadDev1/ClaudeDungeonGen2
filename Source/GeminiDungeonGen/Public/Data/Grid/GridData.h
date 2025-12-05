// GridData.h

#pragma once

#include "CoreMinimal.h"
#include "Engine/StaticMesh.h"
#include "Data/Room/DoorData.h"
#include "GridData.generated.h"

// --- CORE CONSTANT DEFINITION ---
static constexpr float CELL_SIZE = 100.0f;

// Forward declaration for the MasterRoom to use this in USTRUCTs
class UWallData; 
class UFloorData;
class UDoorData;

// --- Enums ---

// Defines the content type of a 100cm grid cell
UENUM(BlueprintType)
enum class EGridCellType : uint8
{
	ECT_Empty 		UMETA(DisplayName = "Empty"),
	ECT_FloorMesh 	UMETA(DisplayName = "Floor Mesh"),
	ECT_Wall 		UMETA(DisplayName = "Wall Boundary"),
	ECT_Doorway 	UMETA(DisplayName = "Doorway Slot")
};

// Defines the four edges of a room for wall placement
// Coordinate System: +X = North (Player Forward), +Y = East, -X = South, -Y = West
UENUM(BlueprintType)
enum class EWallEdge : uint8
{
	North 	UMETA(DisplayName = "North (+X)"),
	South 	UMETA(DisplayName = "South (-X)"),
	East 	UMETA(DisplayName = "East (+Y)"),
	West 	UMETA(DisplayName = "West (-Y)")
};

// --- Mesh Placement Info (Used by Floor and Interior Meshes) ---

// Struct for interior mesh definitions (e.g., clutter, furniture)
USTRUCT(BlueprintType)
struct FMeshPlacementInfo
{
	GENERATED_BODY()

	// The actual mesh asset to be placed. TSoftObjectPtr is good for Data Assets.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Mesh Info")
	TSoftObjectPtr<UStaticMesh> MeshAsset; 

	// The size of the mesh footprint in 100cm cells (e.g., X=2, Y=4 for 200x400cm)
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Mesh Info")
	FIntPoint GridFootprint = FIntPoint(1, 1);

	// Relative weight for randomization (NEW: Clamped between 0.0 and 10.0)
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Mesh Info", meta=(ClampMin="0.0", ClampMax="10.0", UIMin="0.0", UIMax="10.0"))
	float PlacementWeight = 1.0f; // Default remains 1.0f

	// If the mesh is non-square, define allowed rotations (e.g., 0 and 90)
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Mesh Info")
	TArray<int32> AllowedRotations = {0}; 
};

// --- Wall Module Info ---

// Struct for complex wall modules (Base, Middle, Top)
USTRUCT(BlueprintType)
struct FWallModule
{
	GENERATED_BODY()

	// The length of this module in 100cm grid units (e.g., 2 for 200cm wall)
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Wall Info")
	int32 Y_AxisFootprint = 1;

	// Meshes that compose the module, using TSoftObjectPtr for async loading
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Wall Meshes")
	TSoftObjectPtr<UStaticMesh> BaseMesh; 

	// Middle layer 1 (first middle layer, 100cm or 200cm tall)
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Wall Meshes")
	TSoftObjectPtr<UStaticMesh> Middle1Mesh;

	// Middle layer 2 (optional second middle layer, typically 100cm tall)
	// Only used if Middle1Mesh is also assigned
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Wall Meshes")
	TSoftObjectPtr<UStaticMesh> Middle2Mesh;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Wall Meshes")
	TSoftObjectPtr<UStaticMesh> TopMesh;
	
	// Placement weight (NEW: Clamped between 0.0 and 10.0)
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Wall Info", meta=(ClampMin="0.0", ClampMax="10.0", UIMin="0.0", UIMax="10.0"))
	float PlacementWeight = 1.0f;
};

// --- Forced Wall Placement (Designer Override System) ---

// Struct for placing specific wall modules at exact locations
// Allows designers to override random wall generation with precise control
USTRUCT(BlueprintType)
struct FForcedWallPlacement
{
	GENERATED_BODY()

	// Which edge of the room to place this wall on
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Wall Placement")
	EWallEdge Edge = EWallEdge::North;

	// The starting cell index along this edge (0-based)
	// For North/South edges: index along Y-axis (0 to GridSize.Y-1)
	// For East/West edges: index along X-axis (0 to GridSize.X-1)
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Wall Placement")
	int32 StartCell = 0;

	// The exact wall module to place (includes footprint, meshes, and all properties)
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Wall Placement")
	FWallModule WallModule;
};

// --- Forced Empty Region (Designer Override System) ---

// Struct for defining rectangular regions that should remain empty (no floor tiles)
// Used to create L-shapes, T-shapes, courtyards, or any irregular room shape
USTRUCT(BlueprintType)
struct FForcedEmptyRegion
{
	GENERATED_BODY()

	// The starting corner of the rectangular region (inclusive)
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Region")
	FIntPoint StartCell = FIntPoint(0, 0);

	// The ending corner of the rectangular region (inclusive)
	// Order doesn't matter - the system will calculate min/max automatically
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Region")
	FIntPoint EndCell = FIntPoint(0, 0);
};

// --- Door Position Offsets ---

// Struct for fine-tuning door placement positions
// Allows designers to adjust door frame and actor positions independently per door
USTRUCT(BlueprintType)
struct FDoorPositionOffsets
{
	GENERATED_BODY()

	// Offset for the door frame mesh (side pillars) from the wall base position
	// Useful for aligning frames that don't perfectly match wall thickness
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Door Offsets")
	FVector FramePositionOffset = FVector::ZeroVector;

	// Offset for the door actor (functional doorway) from the frame position
	// Useful for centering the door collision/trigger within the frame
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Door Offsets")
	FVector ActorPositionOffset = FVector::ZeroVector;
};

// --- Door Placement (Designer Override System) ---

// Struct for defining fixed door locations on room boundaries
// Doors are placed first, then walls fill the gaps between doors
USTRUCT(BlueprintType)
struct FFixedDoorLocation
{
	GENERATED_BODY()

	// Which wall edge the door should be placed on
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Door Placement")
	EWallEdge WallEdge = EWallEdge::North;

	// Starting cell position along the wall edge
	// For North/South walls: This is the Y coordinate (0 to GridSize.Y-1)
	// For East/West walls: This is the X coordinate (0 to GridSize.X-1)
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Door Placement")
	int32 StartCell = 0;

	// Door asset to use (reference to DoorData asset)
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Door Placement")
	UDoorData* DoorData = nullptr;

	// Position offsets for THIS specific door (allows individual door positioning)
	// If left at zero, no additional offset is applied (beyond base wall alignment)
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Door Placement")
	FDoorPositionOffsets DoorPositionOffsets;
};