// Fill out your copyright notice in the Description page of Project Settings.


#include "DungeonGen/Rooms/MasterRoom.h"
#include "Net/UnrealNetwork.h"
#include "DrawDebugHelpers.h" // Needed for debug drawing
#include "Data/Room/FloorData.h"
#include "Data/Room/RoomData.h"
#include "Data/Room/WallData.h"
#include "Data/Room/CeilingData.h"
#include "Data/Room/RoomShapePreset.h"
#include "Engine/StaticMeshSocket.h"


// Sets default values
AMasterRoom::AMasterRoom()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true; // Essential for multiplayer
	// Ensure the root component is set up for transforms
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
}

void AMasterRoom::GetLifetimeReplicatedProps(TArray<class FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AMasterRoom, GenerationSeed);
}

// --- Editor Debug/Button Logic ---

void AMasterRoom::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	// Check if the property that changed was 'bGenerateRoom'
	FName PropertyName = (PropertyChangedEvent.Property != nullptr) ? PropertyChangedEvent.Property->GetFName() : NAME_None;

	if (PropertyName == GET_MEMBER_NAME_CHECKED(AMasterRoom, bGenerateRoom))
	{
		if (bGenerateRoom)
		{
			RegenerateRoom();
			bGenerateRoom = false; // Reset the button immediately after execution
		}
	}
	
	// IMPORTANT: Call the debug drawing here so it updates instantly in the editor
	if (GIsEditor)
	{
		DrawDebugGrid();
	}
}

// --- Helper: Weighted Random Selection ---

// Selects one FMeshPlacementInfo struct based on placement weights

const FMeshPlacementInfo* AMasterRoom::SelectWeightedMesh(const TArray<FMeshPlacementInfo>& MeshPool, FRandomStream& Stream)
{
	if (MeshPool.Num() == 0)
	{
		return nullptr;
	}

	// Calculate total weight
	float TotalWeight = 0.0f;
	for (const FMeshPlacementInfo& Info : MeshPool)
	{
		TotalWeight += Info.PlacementWeight;
	}

	if (TotalWeight <= 0.0f)
	{
		return &MeshPool[Stream.RandRange(0, MeshPool.Num() - 1)]; // Fallback to uniform random
	}

	// Choose a random point in the total weight range
	float RandomWeight = Stream.FRand() * TotalWeight;
	
	// Find which mesh corresponds to that weight point
	float CurrentWeight = 0.0f;
	for (const FMeshPlacementInfo& Info : MeshPool)
	{
		CurrentWeight += Info.PlacementWeight;
		if (RandomWeight <= CurrentWeight)
		{
			return &Info;
		}
	}

	return &MeshPool.Last(); // Should not be reached, but safe fallback
}

// --- Region Expansion Logic ---

TArray<FIntPoint> AMasterRoom::ExpandForcedEmptyRegions() const
{
	if (!RoomData) return TArray<FIntPoint>();

	const FIntPoint GridSize = RoomData->GridSize;
	TArray<FIntPoint> ExpandedCells;

	// 0. Apply ShapePreset if assigned (preset takes priority, then manual overrides can add to it)
	if (ShapePreset)
	{
		UE_LOG(LogTemp, Warning, TEXT("Applying RoomShapePreset: %s (%s)"), 
			*ShapePreset->ShapeName, 
			*UEnum::GetValueAsString(ShapePreset->ShapeType));

		// Expand preset's empty regions
		for (const FForcedEmptyRegion& Region : ShapePreset->EmptyRegions)
		{
			int32 MinX = FMath::Min(Region.StartCell.X, Region.EndCell.X);
			int32 MaxX = FMath::Max(Region.StartCell.X, Region.EndCell.X);
			int32 MinY = FMath::Min(Region.StartCell.Y, Region.EndCell.Y);
			int32 MaxY = FMath::Max(Region.StartCell.Y, Region.EndCell.Y);

			MinX = FMath::Clamp(MinX, 0, GridSize.X - 1);
			MaxX = FMath::Clamp(MaxX, 0, GridSize.X - 1);
			MinY = FMath::Clamp(MinY, 0, GridSize.Y - 1);
			MaxY = FMath::Clamp(MaxY, 0, GridSize.Y - 1);

			for (int32 Y = MinY; Y <= MaxY; ++Y)
			{
				for (int32 X = MinX; X <= MaxX; ++X)
				{
					ExpandedCells.AddUnique(FIntPoint(X, Y));
				}
			}
		}

		// Add preset's empty cells
		for (const FIntPoint& Cell : ShapePreset->EmptyCells)
		{
			if (Cell.X >= 0 && Cell.X < GridSize.X && Cell.Y >= 0 && Cell.Y < GridSize.Y)
			{
				ExpandedCells.AddUnique(Cell);
			}
		}

		UE_LOG(LogTemp, Warning, TEXT("ShapePreset added %d empty cells"), ExpandedCells.Num());
	}

	// 1. Expand all rectangular regions into individual cells (manual overrides)
	for (const FForcedEmptyRegion& Region : ForcedEmptyRegions)
	{
		// Calculate the bounding box (handles any corner order)
		int32 MinX = FMath::Min(Region.StartCell.X, Region.EndCell.X);
		int32 MaxX = FMath::Max(Region.StartCell.X, Region.EndCell.X);
		int32 MinY = FMath::Min(Region.StartCell.Y, Region.EndCell.Y);
		int32 MaxY = FMath::Max(Region.StartCell.Y, Region.EndCell.Y);

		// Clamp to valid grid bounds (safety check)
		MinX = FMath::Clamp(MinX, 0, GridSize.X - 1);
		MaxX = FMath::Clamp(MaxX, 0, GridSize.X - 1);
		MinY = FMath::Clamp(MinY, 0, GridSize.Y - 1);
		MaxY = FMath::Clamp(MaxY, 0, GridSize.Y - 1);

		// Add all cells within the rectangular region
		for (int32 Y = MinY; Y <= MaxY; ++Y)
		{
			for (int32 X = MinX; X <= MaxX; ++X)
			{
				FIntPoint Cell(X, Y);
				ExpandedCells.AddUnique(Cell); // AddUnique prevents duplicates from overlapping regions
			}
		}
	}

	// 2. Add individual forced empty cells (manual overrides)
	for (const FIntPoint& Cell : ForcedEmptyFloorCells)
	{
		// Validate cell is within grid bounds
		if (Cell.X >= 0 && Cell.X < GridSize.X && Cell.Y >= 0 && Cell.Y < GridSize.Y)
		{
			ExpandedCells.AddUnique(Cell);
		}
	}

	return ExpandedCells;
}

void AMasterRoom::RegenerateRoom()
{
	// Server Check: Only the server or the editor should run generation
	if (GetLocalRole() != ROLE_Authority && !IsEditorOnly() && !GIsEditor)
	{
		return;
	}

	if (!RoomData)
	{
		UE_LOG(LogTemp, Warning, TEXT("ADungeonMasterRoom: RoomData is null. Cannot generate."));
		return;
	}
	
	// 1. Clean up and prepare for a new generation pass
	ClearAndResetComponents();

	// 2. Run generation steps
	GenerateFloorAndInterior();
	GenerateWallsAndDoors();
	GenerateCeiling();
	
	// 3. Force bounding box updates on all new and existing components
	for (const auto& Pair : MeshToHISMMap)
	{
		if (UHierarchicalInstancedStaticMeshComponent* HISM = Pair.Value)
		{
			// Forces the component to re-evaluate its spatial boundaries based on new instances
			HISM->UpdateBounds(); 
			HISM->MarkRenderStateDirty(); // Ensures the rendering thread picks up the change
		}
	}

	// In Editor, this is the most reliable way to force a complete bounds update on the actor
#if WITH_EDITOR
	RerunConstructionScripts();
#endif
	
	// 4. Update the debug visuals immediately
	if (GIsEditor)
	{
		DrawDebugGrid();
	}
}

// --- Wall Generation Helper Functions ---

TArray<FIntPoint> AMasterRoom::GetCellsForEdge(EWallEdge Edge) const
{
	TArray<FIntPoint> Cells;
	if (!RoomData) return Cells;
	
	const FIntPoint GridSize = RoomData->GridSize;
	
	// CRITICAL: Use virtual boundary cells OUTSIDE the interior grid
	// Interior cells: 0 to GridSize-1
	// Boundary positions: GridSize (beyond max) and -1 (before min)
	//
	// COORDINATE SYSTEM: North = +X, South = -X, East = +Y, West = -Y
	
	switch (Edge)
	{
		case EWallEdge::North:  // North = +X direction, X = GridSize (beyond max)
			for (int32 Y = 0; Y < GridSize.Y; ++Y)
				Cells.Add(FIntPoint(GridSize.X, Y));
			break;
		
		case EWallEdge::South:  // South = -X direction, X = -1 (before min)
			for (int32 Y = 0; Y < GridSize.Y; ++Y)
				Cells.Add(FIntPoint(-1, Y));
			break;
		
		case EWallEdge::East:   // East = +Y direction, Y = GridSize (beyond max)
			for (int32 X = 0; X < GridSize.X; ++X)
				Cells.Add(FIntPoint(X, GridSize.Y));
			break;
		
		case EWallEdge::West:   // West = -Y direction, Y = -1 (before min)
			for (int32 X = 0; X < GridSize.X; ++X)
				Cells.Add(FIntPoint(X, -1));
			break;
	}
	
	return Cells;
}

FRotator AMasterRoom::GetWallRotationForEdge(EWallEdge Edge) const
{
	// Rotations confirmed from previous project:
	// East: 270° (or -90°), West: 90°, North: 180°, South: 0°
	
	switch (Edge)
	{
		case EWallEdge::East:   // Y = Max, must face West (-Y, into room)
			return FRotator(0.0f, 270.0f, 0.0f);
		
		case EWallEdge::West:   // Y = 0, must face East (+Y, into room)
			return FRotator(0.0f, 90.0f, 0.0f);
		
		case EWallEdge::North:  // X = Max, must face South (-X, into room)
			return FRotator(0.0f, 180.0f, 0.0f);
		
		case EWallEdge::South:  // X = 0, must face North (+X, into room)
			return FRotator(0.0f, 0.0f, 0.0f);
		
		default:
			return FRotator::ZeroRotator;
	}
}

FVector AMasterRoom::CalculateNorthSouthWallPosition(int32 X, int32 StartY, float WallMeshLength, bool bIsNorthWall) const
{
	// COORDINATE SYSTEM: North = +X, South = -X
	// X can now be -1 (South boundary) or GridSize (North boundary)
	FVector BasePosition = GetActorLocation() + FVector(
			X * CELL_SIZE,
			StartY * CELL_SIZE,
			0.0f
		);
	float HalfLength = WallMeshLength / 2.0f;
	
	// Get wall offsets from WallData asset (per-wall-type configuration)
	float NorthOffset = 0.0f;
	float SouthOffset = 0.0f;
	
	if (RoomData && RoomData->WallStyleData.IsValid())
	{
		UWallData* WallData = RoomData->WallStyleData.LoadSynchronous();
		if (WallData)
		{
			NorthOffset = WallData->NorthWallOffsetX;
			SouthOffset = WallData->SouthWallOffsetX;
		}
	}
	
	FVector WallPivotOffset;

	if (bIsNorthWall)  // North wall: +X direction, X = GridSize
	{
		// X = GridSize (e.g., 10 for 10x10 grid)
		// BasePosition.X = 10 * 100 = 1000cm (already at boundary!)
		// Add offset from WallData for fine-tuning
		WallPivotOffset = FVector(
			NorthOffset,    // Offset from WallData asset
			HalfLength,     // Center along Y-axis
			0.0f
		);
	}
	else  // South wall: -X direction, X = -1
	{
		// X = -1
		// BasePosition.X = -1 * 100 = -100cm (before boundary)
		// Add CELL_SIZE + offset from WallData to reach boundary
		WallPivotOffset = FVector(
			CELL_SIZE + SouthOffset,  // Base offset + WallData adjustment
			HalfLength,               // Center along Y-axis
			0.0f
		);
	}

	return BasePosition + WallPivotOffset;
}

FVector AMasterRoom::CalculateEastWestWallPosition(int32 StartX, int32 Y, float WallMeshLength, bool bIsEastWall) const
{
	// COORDINATE SYSTEM: East = +Y, West = -Y
	// Y can now be -1 (West boundary) or GridSize (East boundary)
	FVector BasePosition = GetActorLocation() + FVector(
		StartX * CELL_SIZE,
		Y * CELL_SIZE,
		0.0f
	);
	float HalfLength = WallMeshLength / 2.0f;
	
	// Get wall offsets from WallData asset (per-wall-type configuration)
	float EastOffset = 0.0f;
	float WestOffset = 0.0f;
	
	if (RoomData && RoomData->WallStyleData.IsValid())
	{
		UWallData* WallData = RoomData->WallStyleData.LoadSynchronous();
		if (WallData)
		{
			EastOffset = WallData->EastWallOffsetY;
			WestOffset = WallData->WestWallOffsetY;
		}
	}
	
	FVector WallPivotOffset;

	if (bIsEastWall)  // East wall: +Y direction, Y = GridSize
	{
		// Y = GridSize (e.g., 10 for 10x10 grid)
		// BasePosition.Y = 10 * 100 = 1000cm (already at boundary!)
		// Add offset from WallData for fine-tuning
		WallPivotOffset = FVector(
			HalfLength,     // Center along X-axis
			EastOffset,     // Offset from WallData asset
			0.0f
		);
	}
	else  // West wall: -Y direction, Y = -1
	{
		// Y = -1
		// BasePosition.Y = -1 * 100 = -100cm (before boundary)
		// Add CELL_SIZE + offset from WallData to reach boundary
		WallPivotOffset = FVector(
			HalfLength,               // Center along X-axis
			CELL_SIZE + WestOffset,   // Base offset + WallData adjustment
			0.0f
		);
	}

	return BasePosition + WallPivotOffset;
}

FVector AMasterRoom::CalculateDoorPosition(EWallEdge Edge, int32 StartCell, float DoorWidth) const
{
	// CRITICAL: Doors use INTERIOR cells (0 to GridSize-1), NOT boundary cells!
	// This keeps doors snapped to floor edges, independent of wall positioning
	//
	// COORDINATE SYSTEM: North = +X, South = -X, East = +Y, West = -Y
	//
	// NOTE: This returns the position of the FIRST pillar (at StartCell).
	// The second pillar position is calculated separately based on footprint.
	
	if (!RoomData) return FVector::ZeroVector;
	
	const FIntPoint GridSize = RoomData->GridSize;
	FVector BasePosition = GetActorLocation();
	FVector DoorPivotOffset;
	
	switch (Edge)
	{
		case EWallEdge::North:  // North = +X boundary (last interior cell)
		{
			// Use last interior cell (GridSize.X - 1) for floor alignment
			int32 X = GridSize.X - 1;
			int32 Y = StartCell;
			
			BasePosition += FVector(X * CELL_SIZE, Y * CELL_SIZE, 0.0f);
			DoorPivotOffset = FVector(
				CELL_SIZE / 2.0f,  // Center on cell X
				CELL_SIZE / 2.0f,  // Center on cell Y (pillar position)
				0.0f
			);
			break;
		}
		
		case EWallEdge::South:  // South = -X boundary (first interior cell)
		{
			// Use first interior cell (0) for floor alignment
			int32 X = 0;
			int32 Y = StartCell;
			
			BasePosition += FVector(X * CELL_SIZE, Y * CELL_SIZE, 0.0f);
			DoorPivotOffset = FVector(
				CELL_SIZE / 2.0f,  // Center on cell X
				CELL_SIZE / 2.0f,  // Center on cell Y (pillar position)
				0.0f
			);
			break;
		}
		
		case EWallEdge::East:   // East = +Y boundary (last interior cell)
		{
			// Use last interior cell (GridSize.Y - 1) for floor alignment
			int32 X = StartCell;
			int32 Y = GridSize.Y - 1;
			
			BasePosition += FVector(X * CELL_SIZE, Y * CELL_SIZE, 0.0f);
			DoorPivotOffset = FVector(
				CELL_SIZE / 2.0f,  // Center on cell X (pillar position)
				CELL_SIZE / 2.0f,  // Center on cell Y
				0.0f
			);
			break;
		}
		
		case EWallEdge::West:   // West = -Y boundary (first interior cell)
		{
			// Use first interior cell (0) for floor alignment
			int32 X = StartCell;
			int32 Y = 0;
			
			BasePosition += FVector(X * CELL_SIZE, Y * CELL_SIZE, 0.0f);
			DoorPivotOffset = FVector(
				CELL_SIZE / 2.0f,  // Center on cell X (pillar position)
				CELL_SIZE / 2.0f,  // Center on cell Y
				0.0f
			);
			break;
		}
	}
	
	// Return base position with door pivot offset
	// Per-door offsets are applied separately during door spawning
	return BasePosition + DoorPivotOffset;
}

void AMasterRoom::FillWallSegment(EWallEdge Edge, int32 SegmentStart, int32 SegmentLength, FRandomStream& Stream)
{
	if (!RoomData || !RoomData->WallStyleData) return;
	
	UWallData* WallData = RoomData->WallStyleData.LoadSynchronous();
	if (WallData->AvailableWallModules.Num() == 0) return;
	
	TArray<FIntPoint> EdgeCells = GetCellsForEdge(Edge);
	if (EdgeCells.Num() == 0 || SegmentStart < 0 || SegmentStart >= EdgeCells.Num()) return;
	
	FRotator WallRotation = GetWallRotationForEdge(Edge);
	bool bIsNorthWall = (Edge == EWallEdge::North);
	bool bIsEastWall = (Edge == EWallEdge::East);
	
	// Greedy bin packing: largest module first
	int32 RemainingCells = SegmentLength;
	int32 CurrentCell = SegmentStart;
	
	while (RemainingCells > 0)
	{
		// Find the largest module that fits
		const FWallModule* BestModule = nullptr;
		
		for (const FWallModule& Module : WallData->AvailableWallModules)
		{
			if (Module.Y_AxisFootprint <= RemainingCells)
			{
				if (!BestModule || Module.Y_AxisFootprint > BestModule->Y_AxisFootprint)
				{
					BestModule = &Module;
				}
			}
		}
		
		if (!BestModule) break;  // No module fits remaining space
		
		// Load base mesh
		UStaticMesh* BaseMesh = BestModule->BaseMesh.LoadSynchronous();
		if (!BaseMesh) break;
		
		// Calculate position based on wall edge using the corrected helper functions
		FVector Position;
		float WallMeshLength = BestModule->Y_AxisFootprint * CELL_SIZE;
		
		if (bIsNorthWall || Edge == EWallEdge::South)
		{
			// North/South walls: Use Y coordinate from EdgeCells
			int32 X = EdgeCells[CurrentCell].X;
			int32 StartY = EdgeCells[CurrentCell].Y;
			Position = CalculateNorthSouthWallPosition(X, StartY, WallMeshLength, bIsNorthWall);
		}
		else  // East or West wall
		{
			// East/West walls: Use X coordinate from EdgeCells
			int32 StartX = EdgeCells[CurrentCell].X;
			int32 Y = EdgeCells[CurrentCell].Y;
			Position = CalculateEastWestWallPosition(StartX, Y, WallMeshLength, bIsEastWall);
		}
		
		// Base walls spawn at floor level (Z=0)
		// BottomBackCenter socket will be at floor level
		// (No offset needed - mesh origin at floor)
		
		// Place base mesh
		UHierarchicalInstancedStaticMeshComponent* HISM = GetOrCreateHISM(BaseMesh);
		if (HISM)
		{
			FTransform Transform(WallRotation, Position, FVector(1.0f));
			HISM->AddInstance(Transform);
			
			// Track this base wall for Middle/Top spawning
			FWallSegmentInfo SegmentInfo;
			SegmentInfo.Edge = Edge;
			SegmentInfo.StartCell = CurrentCell;
			SegmentInfo.SegmentLength = BestModule->Y_AxisFootprint;
			SegmentInfo.BaseTransform = Transform;
			SegmentInfo.BaseMesh = BaseMesh;
			SegmentInfo.WallModule = BestModule;
			PlacedBaseWalls.Add(SegmentInfo);
		}
		
		// Advance to next segment
		RemainingCells -= BestModule->Y_AxisFootprint;
		CurrentCell += BestModule->Y_AxisFootprint;
	}
}

void AMasterRoom::DrawDebugGrid()
{
	if (!RoomData) return;

	const FIntPoint GridSize = RoomData->GridSize;
	const FVector ActorLocation = GetActorLocation();
	const UWorld* World = GetWorld();
	if (!World) return;

	// 1. Draw Grid Lines (Green)
	
	// Draw X-Axis lines
	for (int32 X = 0; X <= GridSize.X; ++X)
	{
		FVector Start = ActorLocation + FVector(X * CELL_SIZE, 0.0f, 0.0f);
		FVector End = ActorLocation + FVector(X * CELL_SIZE, GridSize.Y * CELL_SIZE, 0.0f);
		DrawDebugLine(World, Start, End, FColor::Green, false, 5.0f, 0, 5.0f);
	}

	// Draw Y-Axis lines
	for (int32 Y = 0; Y <= GridSize.Y; ++Y)
	{
		FVector Start = ActorLocation + FVector(0.0f, Y * CELL_SIZE, 0.0f);
		FVector End = ActorLocation + FVector(GridSize.X * CELL_SIZE, Y * CELL_SIZE, 0.0f);
		DrawDebugLine(World, Start, End, FColor::Green, false, 5.0f, 0, 5.0f);
	}
	
	// 2. Draw Forced Empty Regions (Cyan) - Designer Override Visualization
	
	for (const FForcedEmptyRegion& Region : ForcedEmptyRegions)
	{
		// Calculate the bounding box (handles any corner order)
		int32 MinX = FMath::Min(Region.StartCell.X, Region.EndCell.X);
		int32 MaxX = FMath::Max(Region.StartCell.X, Region.EndCell.X);
		int32 MinY = FMath::Min(Region.StartCell.Y, Region.EndCell.Y);
		int32 MaxY = FMath::Max(Region.StartCell.Y, Region.EndCell.Y);

		// Clamp to valid grid bounds
		MinX = FMath::Clamp(MinX, 0, GridSize.X - 1);
		MaxX = FMath::Clamp(MaxX, 0, GridSize.X - 1);
		MinY = FMath::Clamp(MinY, 0, GridSize.Y - 1);
		MaxY = FMath::Clamp(MaxY, 0, GridSize.Y - 1);

		// Draw each cell in the region with cyan boxes
		for (int32 Y = MinY; Y <= MaxY; ++Y)
		{
			for (int32 X = MinX; X <= MaxX; ++X)
			{
				// Center of the cell
				FVector Center = ActorLocation + FVector(
					(X + 0.5f) * CELL_SIZE, 
					(Y + 0.5f) * CELL_SIZE, 
					40.0f // Lift higher than normal boxes for visibility
				);
				
				// Size of the box (half extent) - slightly larger for emphasis
				FVector Extent(CELL_SIZE / 2.2f, CELL_SIZE / 2.2f, 25.0f);
				
				// Cyan color for forced empty regions
				DrawDebugBox(World, Center, Extent, FQuat::Identity, FColor::Cyan, false, 5.0f, 0, 4.0f);
			}
		}
	}

	// 3. Draw Individual Forced Empty Cells (Cyan with Orange border)
	
	for (const FIntPoint& Cell : ForcedEmptyFloorCells)
	{
		// Validate cell is within grid bounds
		if (Cell.X >= 0 && Cell.X < GridSize.X && Cell.Y >= 0 && Cell.Y < GridSize.Y)
		{
			// Center of the cell
			FVector Center = ActorLocation + FVector(
				(Cell.X + 0.5f) * CELL_SIZE, 
				(Cell.Y + 0.5f) * CELL_SIZE, 
				40.0f // Same height as region boxes
			);
			
			// Inner cyan box
			FVector InnerExtent(CELL_SIZE / 2.2f, CELL_SIZE / 2.2f, 25.0f);
			DrawDebugBox(World, Center, InnerExtent, FQuat::Identity, FColor::Cyan, false, 5.0f, 0, 4.0f);
			
			// Outer orange border to distinguish from region cells
			FVector OuterExtent(CELL_SIZE / 2.0f, CELL_SIZE / 2.0f, 27.0f);
			DrawDebugBox(World, Center, OuterExtent, FQuat::Identity, FColor::Orange, false, 5.0f, 0, 2.0f);
		}
	}
	
	// 4. Draw Cell State Boxes (Red/Blue)
	
	for (int32 Y = 0; Y < GridSize.Y; ++Y)
	{
		for (int32 X = 0; X < GridSize.X; ++X)
		{
			int32 Index = Y * GridSize.X + X;
			if (InternalGridState.IsValidIndex(Index))
			{
				// Center of the cell
				FVector Center = ActorLocation + FVector(
					(X + 0.5f) * CELL_SIZE, 
					(Y + 0.5f) * CELL_SIZE, 
					20.0f // Lift the box slightly above Z=0
				);
				
				// Size of the box (half extent)
				FVector Extent(CELL_SIZE / 2.0f, CELL_SIZE / 2.0f, 20.0f);
				
				FColor BoxColor = (InternalGridState[Index] != EGridCellType::ECT_Empty) ? FColor::Red : FColor::Blue;

				DrawDebugBox(World, Center, Extent, FQuat::Identity, BoxColor, false, 5.0f, 0, 3.0f);
			}
		}
	}
}

// --- Component Management ---

void AMasterRoom::ClearAndResetComponents()
{
	// 1. Clear all instances from existing HISM components
	for (const auto& Pair : MeshToHISMMap)
	{
		if (UHierarchicalInstancedStaticMeshComponent* HISM = Pair.Value)
		{
			HISM->ClearInstances();
		}
	}
	
	// 2. Reset internal grid state
	InternalGridState.Empty();
	if (RoomData)
	{
		int32 TotalCells = RoomData->GridSize.X * RoomData->GridSize.Y;
		// Initialize all cells as empty before generation starts
		InternalGridState.Init(EGridCellType::ECT_Empty, TotalCells); 
	}
}

UHierarchicalInstancedStaticMeshComponent* AMasterRoom::GetOrCreateHISM(UStaticMesh* Mesh)
{
	if (!Mesh) return nullptr;

	// Use a raw pointer for the key since UStaticMesh is a UObject and handles its own lifecycle
	if (UHierarchicalInstancedStaticMeshComponent** HISM_Ptr = MeshToHISMMap.Find(Mesh))
	{
		return *HISM_Ptr;
	}
	else
	{
		// Create a new HISM component for this unique mesh
		FString ComponentName = FString::Printf(TEXT("HISM_%s"), *Mesh->GetName());
		UHierarchicalInstancedStaticMeshComponent* NewHISM = NewObject<UHierarchicalInstancedStaticMeshComponent>(this, FName(*ComponentName));
		
		if (NewHISM)
		{
			NewHISM->SetStaticMesh(Mesh);
			NewHISM->RegisterComponent();
			NewHISM->AttachToComponent(RootComponent, FAttachmentTransformRules::KeepRelativeTransform);
			
			MeshToHISMMap.Add(Mesh, NewHISM);
			return NewHISM;
		}
	}
	return nullptr;
}

// --- Generation Implementation Sketch ---

void AMasterRoom::GenerateFloorAndInterior()
{
	if (!RoomData) return;

	// Use a seeded random stream for predictable generation
	FRandomStream RandomStream(GenerationSeed);
	
	const FIntPoint GridSize = RoomData->GridSize;
	const UFloorData* FloorData = RoomData->FloorStyleData.LoadSynchronous();
	
	if (!FloorData)
	{
		UE_LOG(LogTemp, Warning, TEXT("FloorData failed to load or is null. Cannot generate floor."));
		return;
	}

    // --- PASS 0: DESIGNER OVERRIDES: FORCED PLACEMENTS (NEW) ---
    ExecuteForcedPlacements(RandomStream);
    
    // --- DESIGNER OVERRIDES: FORCED EMPTY CELLS (Uses Regions + Individual Cells) ---
    // Expand all regions and individual cells into a unified list
    TArray<FIntPoint> AllForcedEmptyCells = ExpandForcedEmptyRegions();
    
    // Mark specific cells as reserved (to be empty) before Pass 1 begins
    for (const FIntPoint& EmptyCoord : AllForcedEmptyCells)
    {
        int32 Index = EmptyCoord.Y * GridSize.X + EmptyCoord.X;
        if (InternalGridState.IsValidIndex(Index) && InternalGridState[Index] == EGridCellType::ECT_Empty)
        {
            // Mark cell as a reserved boundary/empty slot
            InternalGridState[Index] = EGridCellType::ECT_Wall; // Using Wall type to indicate reserved boundary for now
        }
    }


    // --- PASS 1: WEIGHTED AND LARGE MESH PLACEMENT (Modified to check overrides) ---

	for (int32 Y = 0; Y < GridSize.Y; ++Y)
	{
		for (int32 X = 0; X < GridSize.X; ++X)
		{
			const int32 Index = Y * GridSize.X + X;
			
			// If cell is occupied OR marked as forced empty (ECT_Wall), skip to the next
			if (InternalGridState[Index] != EGridCellType::ECT_Empty)
			{
				continue;
			}
			
			// ... (A. Weighted Random Selection remains the same) ...
			const FMeshPlacementInfo* MeshToPlaceInfo = SelectWeightedMesh(FloorData->FloorTilePool, RandomStream);
			
			if (!MeshToPlaceInfo || MeshToPlaceInfo->MeshAsset.IsPending())
			{
				if (MeshToPlaceInfo) MeshToPlaceInfo->MeshAsset.LoadSynchronous();
				continue;
			}
			
			UStaticMesh* Mesh = MeshToPlaceInfo->MeshAsset.Get();
			if (!Mesh) continue;

			bool bCanPlace = true;

			// ... (B. Select Rotation and Calculate Rotated Footprint remains the same) ...
            const int32 RandomRotationIndex = RandomStream.RandRange(0, MeshToPlaceInfo->AllowedRotations.Num() - 1);
			const float YawRotation = (float)MeshToPlaceInfo->AllowedRotations[RandomRotationIndex];

			FIntPoint RotatedFootprint = MeshToPlaceInfo->GridFootprint;
			if (FMath::IsNearlyEqual(YawRotation, 90.0f) || FMath::IsNearlyEqual(YawRotation, 270.0f))
			{
				RotatedFootprint = FIntPoint(MeshToPlaceInfo->GridFootprint.Y, MeshToPlaceInfo->GridFootprint.X);
			}

			// C. Bounds and Occupancy Check (Crucially checks against all existing occupations, including forced items)
			if (X + RotatedFootprint.X > GridSize.X || Y + RotatedFootprint.Y > GridSize.Y)
			{
				bCanPlace = false;
			}
			
			if (bCanPlace)
			{
				for (int32 FootY = 0; FootY < RotatedFootprint.Y; ++FootY)
				{
					for (int32 FootX = 0; FootX < RotatedFootprint.X; ++FootX)
					{
						int32 FootIndex = (Y + FootY) * GridSize.X + (X + FootX);
						
						// The main check: if the cell is NOT empty (it could be ECT_FloorMesh, ECT_Wall/Forced Empty)
						if (InternalGridState.IsValidIndex(FootIndex) && InternalGridState[FootIndex] != EGridCellType::ECT_Empty)
						{
							bCanPlace = false;
							break;
						}
					}
					if (!bCanPlace) break;
				}
			}

			// D. Placement and Grid Marking (remains the same)
			if (bCanPlace)
			{
				UHierarchicalInstancedStaticMeshComponent* HISM = GetOrCreateHISM(Mesh);
				if (HISM)
				{
					FVector CenterLocation = FVector(
						(X + RotatedFootprint.X / 2.0f) * CELL_SIZE, 
						(Y + RotatedFootprint.Y / 2.0f) * CELL_SIZE, 
						0.0f
					);
					
					FTransform InstanceTransform(FRotator(0.0f, YawRotation, 0.0f), CenterLocation);
					HISM->AddInstance(InstanceTransform);
					
					// Mark all cells as occupied
					for (int32 FootY = 0; FootY < RotatedFootprint.Y; ++FootY)
					{
						for (int32 FootX = 0; FootX < RotatedFootprint.X; ++FootX)
						{
							int32 FootIndex = (Y + FootY) * GridSize.X + (X + FootX);
							InternalGridState[FootIndex] = EGridCellType::ECT_FloorMesh; 
						}
					}
				}
			}
		}
	}


    // --- PASS 2: GAP FILLING WITH DEFAULT 1x1 TILE (Modified to respect forced empty cells) ---
    
    UStaticMesh* FillerMesh = FloorData->DefaultFillerTile.LoadSynchronous();
    if (FillerMesh)
    {
        UHierarchicalInstancedStaticMeshComponent* HISM = GetOrCreateHISM(FillerMesh);
        
        for (int32 Y = 0; Y < GridSize.Y; ++Y)
        {
            for (int32 X = 0; X < GridSize.X; ++X)
            {
                int32 Index = Y * GridSize.X + X;
                
                // Only place if the cell is still **completely empty** (not ECT_Wall/Forced Empty)
                if (InternalGridState[Index] == EGridCellType::ECT_Empty)
                {
                    // Placement is trivial since it's a 1x1 tile
                    FVector CenterLocation = FVector(
                        (X + 0.5f) * CELL_SIZE, 
                        (Y + 0.5f) * CELL_SIZE, 
                        0.0f
                    );
                    
                    FTransform InstanceTransform(FRotator::ZeroRotator, CenterLocation);
                    HISM->AddInstance(InstanceTransform);
                    
                    // Mark cell as ECT_FloorMesh, it is now filled
                    InternalGridState[Index] = EGridCellType::ECT_FloorMesh; 
                }
            }
        }
    }
}

void AMasterRoom::GenerateWallsAndDoors()
{
	if (!RoomData) return;

	UWallData* WallData = RoomData->WallStyleData.LoadSynchronous();
	if (!WallData) return;

	// Clear OccupancyGrid for fresh generation
	OccupancyGrid.Empty();
	
	// Clear base wall tracking for Middle/Top spawning
	PlacedBaseWalls.Empty();
	
	// Clear procedural doors from previous generation to prevent accumulation
	// Note: Manual doors in FixedDoorLocations should be cleared by designer if needed
	if (bEnableProceduralDoors)
	{
		FixedDoorLocations.Empty();
		UE_LOG(LogTemp, Warning, TEXT("Cleared FixedDoorLocations (procedural mode - will regenerate)"));
	}

	// === EXTENSIVE LOGGING START ===
	UE_LOG(LogTemp, Warning, TEXT("========================================"));
	UE_LOG(LogTemp, Warning, TEXT("GenerateWallsAndDoors() STARTED"));
	UE_LOG(LogTemp, Warning, TEXT("Total FixedDoorLocations entries: %d"), FixedDoorLocations.Num());
	
	// Log each door entry BEFORE processing
	for (int32 i = 0; i < FixedDoorLocations.Num(); ++i)
	{
		const FFixedDoorLocation& Door = FixedDoorLocations[i];
		UE_LOG(LogTemp, Warning, TEXT("  Door Entry [%d]: Edge=%d, StartCell=%d, DoorData=%s"), 
			i, 
			(int32)Door.WallEdge, 
			Door.StartCell,
			Door.DoorData ? *Door.DoorData->GetName() : TEXT("NULL"));
		if (Door.DoorData)
		{
			UE_LOG(LogTemp, Warning, TEXT("    FrameFootprintY=%d, FrameSideMesh=%s"), 
				Door.DoorData->FrameFootprintY,
				Door.DoorData->FrameSideMesh.IsValid() ? TEXT("Valid") : TEXT("NULL"));
		}
	}
	UE_LOG(LogTemp, Warning, TEXT("========================================"));
	// === EXTENSIVE LOGGING END ===

	FRandomStream RandomStream(GenerationSeed);

	// --- Procedural Door Placement (if enabled) ---
	// IMPORTANT: This must happen BEFORE edge processing so doors are in FixedDoorLocations
	// when walls are placed, allowing walls to respect door positions
	if (bEnableProceduralDoors)
	{
		PlaceProceduralDoors(RandomStream);
		UE_LOG(LogTemp, Warning, TEXT("Procedural doors placed. FixedDoorLocations now has %d entries"), 
			FixedDoorLocations.Num());
	}

	// --- Forced Wall Placement (Designer Override) ---
	// Place forced walls before random generation so they take priority
	PlaceForcedWalls();

	TArray<EWallEdge> Edges = {EWallEdge::North, EWallEdge::South, EWallEdge::East, EWallEdge::West};
	
	for (EWallEdge Edge : Edges)
	{
		TArray<FIntPoint> EdgeCells = GetCellsForEdge(Edge);
		if (EdgeCells.Num() == 0) continue;

		TArray<bool> CellOccupied;
		CellOccupied.SetNum(EdgeCells.Num());
		for (int32 i = 0; i < EdgeCells.Num(); ++i)
		{
			// Check if cell is already occupied (by forced walls or other elements)
			CellOccupied[i] = OccupancyGrid.Contains(EdgeCells[i]);
		}

		// --- PASS 1: Mark Door Cells and Place ONLY Side Frames ---
		int32 DoorsOnThisEdge = 0;
		UE_LOG(LogTemp, Warning, TEXT(">>> Processing Edge %d <<<"), (int32)Edge);
		
		for (const FFixedDoorLocation& DoorLoc : FixedDoorLocations)
		{
			UE_LOG(LogTemp, Warning, TEXT("  Checking door: Edge=%d vs %d, DoorData=%s"), 
				(int32)DoorLoc.WallEdge, (int32)Edge, DoorLoc.DoorData ? TEXT("Valid") : TEXT("NULL"));
			
			if (DoorLoc.WallEdge != Edge || !DoorLoc.DoorData) 
			{
				UE_LOG(LogTemp, Warning, TEXT("    SKIPPED (wrong edge or no data)"));
				continue;
			}
			
			DoorsOnThisEdge++;
			UE_LOG(LogTemp, Warning, TEXT(">>> PLACING DOOR #%d on edge %d <<<"), DoorsOnThisEdge, (int32)Edge);
			UE_LOG(LogTemp, Warning, TEXT("  StartCell: %d"), DoorLoc.StartCell);

			UDoorData* DoorData = DoorLoc.DoorData;
			int32 DoorFootprint = FMath::Max(1, DoorData->FrameFootprintY);
			UE_LOG(LogTemp, Warning, TEXT("  DoorFootprint: %d"), DoorFootprint);
			
			// Load door frame side mesh only
			UStaticMesh* FrameSideMesh = DoorData->FrameSideMesh.LoadSynchronous();
			UE_LOG(LogTemp, Warning, TEXT("  FrameSideMesh: %s"), FrameSideMesh ? *FrameSideMesh->GetName() : TEXT("NULL"));
			
			FRotator WallRotation = GetWallRotationForEdge(Edge);
			// NOTE: If using the designer-editable offset (from previous suggestion), apply it here:
			// FRotator FinalRotation = WallRotation + DoorData->FrameRotationOffset;
			FRotator FinalRotation = WallRotation; 

			bool bIsNorthWall = (Edge == EWallEdge::North);
			bool bIsEastWall = (Edge == EWallEdge::East);
			
			// --- Placement of Door Frame Meshes ---
			if (FrameSideMesh)
			{
				UHierarchicalInstancedStaticMeshComponent* HISM_Side = GetOrCreateHISM(FrameSideMesh);
				if (HISM_Side)
				{
					int32 InstancesBefore = HISM_Side->GetInstanceCount();
					UE_LOG(LogTemp, Warning, TEXT("  HISM Component: %s (instances BEFORE adding: %d)"), 
						*HISM_Side->GetName(), InstancesBefore);
					
					// Apply door rotation (wall rotation + any door-specific offset)
					FRotator DoorRotation = WallRotation + DoorData->FrameRotationOffset;

					// CRITICAL: For COMPLETE door frame meshes (not separate pillars)
					// Place ONE instance centered across the door span
					
					// Calculate center position of the door span
					// For 2-cell door at StartCell=1: center between cells 1 and 2 = Y=200
					float DoorSpanCenter;
					if (Edge == EWallEdge::North || Edge == EWallEdge::South)
					{
						// North/South doors span along Y-axis
						DoorSpanCenter = (DoorLoc.StartCell * CELL_SIZE) + ((DoorFootprint * CELL_SIZE) / 2.0f);
					}
					else
					{
						// East/West doors span along X-axis
						DoorSpanCenter = (DoorLoc.StartCell * CELL_SIZE) + ((DoorFootprint * CELL_SIZE) / 2.0f);
					}
					
					// Get the base position using the middle of the door span
					float MiddleCell = DoorLoc.StartCell + (DoorFootprint / 2.0f);
					FVector DoorCenterPos = CalculateDoorPosition(Edge, MiddleCell, 0.0f);
					
					// Door frames spawn at floor level (Z=0), matching base walls
					// BottomBackCenter socket will be at floor level
					
					// Apply per-door frame position offset
					DoorCenterPos += DoorLoc.DoorPositionOffsets.FramePositionOffset;
					
					UE_LOG(LogTemp, Warning, TEXT("  Placing COMPLETE door frame: Footprint=%d cells, StartCell=%d"), 
						DoorFootprint, DoorLoc.StartCell);
					UE_LOG(LogTemp, Warning, TEXT("  Calculated middle cell: %.2f"), MiddleCell);
					UE_LOG(LogTemp, Warning, TEXT("  Applied FramePositionOffset: %s"), 
						*DoorLoc.DoorPositionOffsets.FramePositionOffset.ToString());
					
					// Place ONE instance of the complete door frame
					HISM_Side->AddInstance(FTransform(DoorRotation, DoorCenterPos, FVector(1.0f)));
					UE_LOG(LogTemp, Warning, TEXT("    >>> PLACED DOOR FRAME at position: %s (instances now: %d)"), 
						*DoorCenterPos.ToString(), HISM_Side->GetInstanceCount());
					
					UE_LOG(LogTemp, Warning, TEXT("  TOTAL instances added for this door: 1 (Complete Frame)"));
					UE_LOG(LogTemp, Warning, TEXT("  Final HISM instance count: %d"), HISM_Side->GetInstanceCount());
				}
				else
				{
					UE_LOG(LogTemp, Error, TEXT("  ERROR: Failed to get HISM component!"));
				}
			}
			else
			{
				UE_LOG(LogTemp, Error, TEXT("  ERROR: FrameSideMesh is NULL!"));
			}
			
			// Mark the cells as occupied by this door (essential for wall filling logic)
			UE_LOG(LogTemp, Warning, TEXT("  Marking door cells as occupied: StartCell=%d, Footprint=%d"), 
				DoorLoc.StartCell, DoorFootprint);
			
			for (int32 i = 0; i < DoorFootprint && (DoorLoc.StartCell + i) < EdgeCells.Num(); ++i)
			{
				int32 CellIndex = DoorLoc.StartCell + i;
				if (CellIndex >= 0 && CellIndex < CellOccupied.Num())
				{
					CellOccupied[CellIndex] = true;
					
					// Also mark in OccupancyGrid for global tracking
					const FIntPoint& CellPos = EdgeCells[CellIndex];
					OccupancyGrid.Add(CellPos, EGridCellType::ECT_Doorway);
					
					UE_LOG(LogTemp, Warning, TEXT("    Marked cell %d as OCCUPIED (pos: %s)"), 
						CellIndex, *CellPos.ToString());
				}
			}
			
			// TODO: Add logic here to spawn the ADoorway actor using DoorData->DoorwayClass
			// When implemented, apply DoorLoc.DoorPositionOffsets.ActorPositionOffset to actor position:
			// FVector ActorPos = DoorCenterPos + DoorLoc.DoorPositionOffsets.ActorPositionOffset;
			// GetWorld()->SpawnActor<ADoorway>(DoorData->DoorwayClass, ActorPos, DoorRotation);
		}

		// --- PASS 2: Find continuous wall segments (non-door cells) and fill them ---
		UE_LOG(LogTemp, Warning, TEXT(">>> PASS 2: Finding wall segments (skipping occupied cells) <<<"));
		int32 SegmentStart = -1;
		for (int32 i = 0; i < CellOccupied.Num(); ++i)
		{
            // ... (Wall filling logic remains the same) ...
            if (!CellOccupied[i])
			{
				if (SegmentStart == -1)
				{
					SegmentStart = i;
				}
			}
			else
			{
				UE_LOG(LogTemp, Warning, TEXT("  Cell %d is OCCUPIED (skipping for walls)"), i);
				if (SegmentStart != -1)
				{
					int32 SegmentLength = i - SegmentStart;
					UE_LOG(LogTemp, Warning, TEXT("  Found wall segment: Start=%d, Length=%d"), 
						SegmentStart, SegmentLength);
					FillWallSegment(Edge, SegmentStart, SegmentLength, RandomStream);
					SegmentStart = -1;
				}
			}
		}

		// Handle final segment if it extends to the end
		if (SegmentStart != -1)
		{
			int32 SegmentLength = CellOccupied.Num() - SegmentStart;
			UE_LOG(LogTemp, Warning, TEXT("  Found final wall segment: Start=%d, Length=%d"), 
				SegmentStart, SegmentLength);
			FillWallSegment(Edge, SegmentStart, SegmentLength, RandomStream);
		}
	}
	
	// --- Spawn Middle & Top Wall Layers ---
	// Now that all base walls are placed and tracked, spawn stacked layers
	SpawnMiddleWalls();
	SpawnTopWalls();
	
	// --- Spawn Corner Pieces ---
	// Place corner meshes at the 4 room corners
	SpawnCorners();
	
	UE_LOG(LogTemp, Warning, TEXT("========================================"));
	UE_LOG(LogTemp, Warning, TEXT("GenerateWallsAndDoors() COMPLETE"));
	UE_LOG(LogTemp, Warning, TEXT("Base walls placed: %d segments"), PlacedBaseWalls.Num());
	UE_LOG(LogTemp, Warning, TEXT("========================================"));
}

void AMasterRoom::PlaceProceduralDoors(FRandomStream& Stream)
{
	if (!RoomData) return;
	
	// Load the DoorData pack
	UDoorData* DoorData = RoomData->DoorStyleData.LoadSynchronous();
	if (!DoorData)
	{
		UE_LOG(LogTemp, Warning, TEXT("PlaceProceduralDoors: No DoorData available"));
		return;
	}
	
	// Log pool info (empty pool is OK - will use DoorData itself as fallback)
	int32 PoolSize = DoorData->DoorStylePool.Num();
	UE_LOG(LogTemp, Warning, TEXT("========================================"));
	
	// Check if specific edges are required (overrides randomization)
	TArray<EWallEdge> EdgesToProcess;
	bool bUsingRequiredEdges = RequiredDoorEdges.Num() > 0;
	
	if (bUsingRequiredEdges)
	{
		// REQUIRED EDGES MODE: Use specified edges exactly
		EdgesToProcess = RequiredDoorEdges;
		UE_LOG(LogTemp, Warning, TEXT("PROCEDURAL DOOR PLACEMENT - Required Edges Mode"));
		UE_LOG(LogTemp, Warning, TEXT("Door Pool Size: %d %s"), PoolSize, 
			(PoolSize == 0) ? TEXT("(using fallback - single door mode)") : TEXT(""));
		UE_LOG(LogTemp, Warning, TEXT("Required Edges: %d (ignoring Min/Max)"), EdgesToProcess.Num());
		for (int32 i = 0; i < EdgesToProcess.Num(); ++i)
		{
			UE_LOG(LogTemp, Warning, TEXT("  Edge %d: %d"), i, (int32)EdgesToProcess[i]);
		}
	}
	else
	{
		// RANDOMIZATION MODE: Use Min/Max range
		int32 NumDoorsToPlace = Stream.RandRange(MinProceduralDoors, MaxProceduralDoors);
		
		UE_LOG(LogTemp, Warning, TEXT("PROCEDURAL DOOR PLACEMENT - Randomized Mode"));
		UE_LOG(LogTemp, Warning, TEXT("Door Pool Size: %d %s"), PoolSize, 
			(PoolSize == 0) ? TEXT("(using fallback - single door mode)") : TEXT(""));
		UE_LOG(LogTemp, Warning, TEXT("Target Doors: %d (Min=%d, Max=%d)"), 
			NumDoorsToPlace, MinProceduralDoors, MaxProceduralDoors);
		
		// Create array of all edges and shuffle it for random selection
		TArray<EWallEdge> AllEdges = {EWallEdge::North, EWallEdge::South, EWallEdge::East, EWallEdge::West};
		
		// Fisher-Yates shuffle algorithm for random edge order
		for (int32 i = AllEdges.Num() - 1; i > 0; --i)
		{
			int32 j = Stream.RandRange(0, i);
			AllEdges.Swap(i, j);
		}
		
		UE_LOG(LogTemp, Warning, TEXT("Shuffled edge order: %d, %d, %d, %d"), 
			(int32)AllEdges[0], (int32)AllEdges[1], (int32)AllEdges[2], (int32)AllEdges[3]);
		
		// Take first N edges from shuffled array
		for (int32 i = 0; i < NumDoorsToPlace && i < AllEdges.Num(); ++i)
		{
			EdgesToProcess.Add(AllEdges[i]);
		}
	}
	
	UE_LOG(LogTemp, Warning, TEXT("========================================"));
	
	// Place doors on all edges in EdgesToProcess
	int32 TotalDoorsPlaced = 0;
	
	for (int32 EdgeIndex = 0; EdgeIndex < EdgesToProcess.Num(); ++EdgeIndex)
	{
		EWallEdge Edge = EdgesToProcess[EdgeIndex];
		UE_LOG(LogTemp, Warning, TEXT(">>> Attempting to place door on Edge %d <<<"), (int32)Edge);
		
		// Get all valid locations on this edge (gaps between fixed doors)
		TArray<TPair<int32, int32>> ValidSpots = GetValidDoorLocations(Edge);
		
		if (ValidSpots.Num() == 0)
		{
			UE_LOG(LogTemp, Warning, TEXT("  No valid gaps - skipping this edge"));
			continue;
		}
		
		UE_LOG(LogTemp, Warning, TEXT("  Found %d valid gaps"), ValidSpots.Num());
		
		// Pick a random gap
		int32 RandomGapIndex = Stream.RandRange(0, ValidSpots.Num() - 1);
		const TPair<int32, int32>& SelectedGap = ValidSpots[RandomGapIndex];
		
		int32 GapStart = SelectedGap.Key;
		int32 GapSize = SelectedGap.Value;
		
		UE_LOG(LogTemp, Warning, TEXT("  Selected gap #%d: StartCell=%d, Size=%d"), 
			RandomGapIndex, GapStart, GapSize);
		
		// Select a size-appropriate door from pool
		UDoorData* SelectedDoor = nullptr;
		int32 Attempts = 0;
		const int32 MaxAttempts = 10;
		
		while (Attempts < MaxAttempts && !SelectedDoor)
		{
			UDoorData* CandidateDoor = SelectRandomDoorFromPool(Stream);
			if (CandidateDoor)
			{
				int32 DoorFootprint = FMath::Max(1, CandidateDoor->FrameFootprintY);
				
				// Check if door fits in gap
				if (DoorFootprint <= GapSize)
				{
					SelectedDoor = CandidateDoor;
					UE_LOG(LogTemp, Warning, TEXT("    Selected door with footprint %d"), DoorFootprint);
					break;
				}
			}
			Attempts++;
		}
		
		if (!SelectedDoor)
		{
			UE_LOG(LogTemp, Warning, TEXT("    FAILED: Could not find door that fits after %d attempts"), MaxAttempts);
			continue;
		}
		
		// Calculate random placement position within gap
		int32 DoorFootprint = FMath::Max(1, SelectedDoor->FrameFootprintY);
		int32 MaxOffset = GapSize - DoorFootprint;
		int32 RandomOffset = (MaxOffset > 0) ? Stream.RandRange(0, MaxOffset) : 0;
		int32 PlacementCell = GapStart + RandomOffset;
		
		// Add to FixedDoorLocations array
		FFixedDoorLocation NewDoor;
		NewDoor.WallEdge = Edge;
		NewDoor.StartCell = PlacementCell;
		NewDoor.DoorData = SelectedDoor;
		
		FixedDoorLocations.Add(NewDoor);
		TotalDoorsPlaced++;
		
		UE_LOG(LogTemp, Warning, TEXT("    PLACED door at cell %d (footprint %d)"), PlacementCell, DoorFootprint);
	}
	
	UE_LOG(LogTemp, Warning, TEXT("========================================"));
	UE_LOG(LogTemp, Warning, TEXT("PROCEDURAL PLACEMENT COMPLETE"));
	if (bUsingRequiredEdges)
	{
		UE_LOG(LogTemp, Warning, TEXT("Mode: Required Edges | Target: %d doors | Placed: %d doors"), 
			EdgesToProcess.Num(), TotalDoorsPlaced);
		if (TotalDoorsPlaced < EdgesToProcess.Num())
		{
			UE_LOG(LogTemp, Warning, TEXT("WARNING: Some required edges had no valid gaps!"));
		}
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("Mode: Randomized | Target: %d doors | Placed: %d doors"), 
			EdgesToProcess.Num(), TotalDoorsPlaced);
		if (TotalDoorsPlaced < EdgesToProcess.Num())
		{
			UE_LOG(LogTemp, Warning, TEXT("Note: Some edges had no valid gaps"));
		}
	}
	UE_LOG(LogTemp, Warning, TEXT("Doors added to FixedDoorLocations - walls will respect them"));
	UE_LOG(LogTemp, Warning, TEXT("========================================"));
}

void AMasterRoom::ExecuteForcedPlacements(FRandomStream& Stream)
{
	if (!RoomData) return;

	const FIntPoint GridSize = RoomData->GridSize;
	
	// Iterate through the map of designer-forced placements (Pass 0)
	for (const auto& Pair : ForcedInteriorPlacements)
	{
		const FIntPoint StartCoord = Pair.Key;
		const FMeshPlacementInfo& MeshToPlaceInfo = Pair.Value;
		
		bool bCanPlace = true; // Assume placement is possible until proven otherwise

		// 1. Load Mesh and Check Validity
		UStaticMesh* Mesh = MeshToPlaceInfo.MeshAsset.LoadSynchronous();
		if (!Mesh)
		{
			UE_LOG(LogTemp, Warning, TEXT("Forced Placement failed: Mesh asset for (%d, %d) is NULL. Skipping."), StartCoord.X, StartCoord.Y);
			continue;
		}

		// 2. Select Rotation and Calculate Rotated Footprint (Uses Stream for rotation)
		const int32 RandomRotationIndex = Stream.RandRange(0, MeshToPlaceInfo.AllowedRotations.Num() - 1);
		const float YawRotation = (float)MeshToPlaceInfo.AllowedRotations[RandomRotationIndex];

		FIntPoint RotatedFootprint = MeshToPlaceInfo.GridFootprint;
		if (FMath::IsNearlyEqual(YawRotation, 90.0f) || FMath::IsNearlyEqual(YawRotation, 270.0f))
		{
			// Swap dimensions for 90 or 270 degree rotation
			RotatedFootprint = FIntPoint(MeshToPlaceInfo.GridFootprint.Y, MeshToPlaceInfo.GridFootprint.X);
		}

		// 3. Bounds Check
		if (StartCoord.X < 0 || StartCoord.Y < 0 || 
			StartCoord.X + RotatedFootprint.X > GridSize.X || 
			StartCoord.Y + RotatedFootprint.Y > GridSize.Y)
		{
			bCanPlace = false;
			UE_LOG(LogTemp, Warning, TEXT("Forced Placement failed: Mesh at (%d, %d) is out of bounds."), StartCoord.X, StartCoord.Y);
		}
		
		// 4. Overlap Check (Checks against previously placed forced items)
		if (bCanPlace)
		{
			for (int32 FootY = 0; FootY < RotatedFootprint.Y; ++FootY)
			{
				for (int32 FootX = 0; FootX < RotatedFootprint.X; ++FootX)
				{
					int32 FootIndex = (StartCoord.Y + FootY) * GridSize.X + (StartCoord.X + FootX);
					
					// If the target cell is already occupied (by another forced placement), fail.
					if (InternalGridState.IsValidIndex(FootIndex) && InternalGridState[FootIndex] != EGridCellType::ECT_Empty)
					{
						bCanPlace = false;
						UE_LOG(LogTemp, Warning, TEXT("Forced Placement failed: Mesh at (%d, %d) overlaps existing forced item."), StartCoord.X, StartCoord.Y);
						break;
					}
				}
				if (!bCanPlace) break;
			}
		}

		// 5. Placement and Grid Marking (Executed ONLY if all checks passed)
		if (bCanPlace)
		{
			UHierarchicalInstancedStaticMeshComponent* HISM = GetOrCreateHISM(Mesh);
			if (HISM)
			{
				// Calculate position (Center Pivot assumed)
				FVector CenterLocation = FVector(
					(StartCoord.X + RotatedFootprint.X / 2.0f) * CELL_SIZE, 
					(StartCoord.Y + RotatedFootprint.Y / 2.0f) * CELL_SIZE, 
					0.0f
				);
				
				FTransform InstanceTransform(FRotator(0.0f, YawRotation, 0.0f), CenterLocation);
				
				// CRITICAL: Add the instance visually
				HISM->AddInstance(InstanceTransform);
				
				// CRITICAL: Mark all covered cells as occupied (Red in debug view)
				for (int32 FootY = 0; FootY < RotatedFootprint.Y; ++FootY)
				{
					for (int32 FootX = 0; FootX < RotatedFootprint.X; ++FootX)
					{
						int32 FootIndex = (StartCoord.Y + FootY) * GridSize.X + (StartCoord.X + FootX);
						
						if (InternalGridState.IsValidIndex(FootIndex)) 
						{
							InternalGridState[FootIndex] = EGridCellType::ECT_FloorMesh; 
						}
					}
				}
			}
		}
	}
}

// ==================================================================================
// FORCED WALL PLACEMENT (Designer Override System)
// ==================================================================================

void AMasterRoom::PlaceForcedWalls()
{
	if (ForcedWalls.Num() == 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("No forced walls to place"));
		return;
	}

	UE_LOG(LogTemp, Warning, TEXT("========================================"));
	UE_LOG(LogTemp, Warning, TEXT("PLACING FORCED WALLS"));
	UE_LOG(LogTemp, Warning, TEXT("Total forced walls: %d"), ForcedWalls.Num());

	int32 WallsPlaced = 0;
	int32 WallsSkipped = 0;

	for (int32 i = 0; i < ForcedWalls.Num(); i++)
	{
		const FForcedWallPlacement& ForcedWall = ForcedWalls[i];
		const FWallModule& Module = ForcedWall.WallModule;

		UE_LOG(LogTemp, Warning, TEXT("  Forced Wall [%d]: Edge=%d, StartCell=%d, Footprint=%d"),
			i, (int32)ForcedWall.Edge, ForcedWall.StartCell, Module.Y_AxisFootprint);

		// Load base mesh
		UStaticMesh* BaseMesh = Module.BaseMesh.LoadSynchronous();
		if (!BaseMesh)
		{
			UE_LOG(LogTemp, Error, TEXT("    SKIPPED: BaseMesh failed to load"));
			WallsSkipped++;
			continue;
		}

		// Get edge cells to validate placement
		TArray<FIntPoint> EdgeCells = GetCellsForEdge(ForcedWall.Edge);
		if (EdgeCells.Num() == 0)
		{
			UE_LOG(LogTemp, Error, TEXT("    SKIPPED: No cells on edge %d"), (int32)ForcedWall.Edge);
			WallsSkipped++;
			continue;
		}

		// Check if placement is valid (cells available)
		int32 Footprint = Module.Y_AxisFootprint;
		if (ForcedWall.StartCell < 0 || ForcedWall.StartCell + Footprint > EdgeCells.Num())
		{
			UE_LOG(LogTemp, Error, TEXT("    SKIPPED: Invalid cell range (StartCell=%d, Footprint=%d, EdgeLength=%d)"),
				ForcedWall.StartCell, Footprint, EdgeCells.Num());
			WallsSkipped++;
			continue;
		}

		// Check if cells are already occupied (by doors or other forced walls)
		bool bCellsOccupied = false;
		for (int32 j = 0; j < Footprint; j++)
		{
			int32 CellIndex = ForcedWall.StartCell + j;
			FIntPoint CellCoord = EdgeCells[CellIndex];
			if (OccupancyGrid.Contains(CellCoord))
			{
				UE_LOG(LogTemp, Warning, TEXT("    Cell %d (coord %d,%d) already occupied!"),
					CellIndex, CellCoord.X, CellCoord.Y);
				bCellsOccupied = true;
				break;
			}
		}

		if (bCellsOccupied)
		{
			UE_LOG(LogTemp, Error, TEXT("    SKIPPED: Cells already occupied"));
			WallsSkipped++;
			continue;
		}

		// Calculate wall position
		FVector WallPosition;
		bool bIsNorthWall = (ForcedWall.Edge == EWallEdge::North);
		bool bIsEastWall = (ForcedWall.Edge == EWallEdge::East);

		float WallLength = Footprint * CELL_SIZE;

		if (ForcedWall.Edge == EWallEdge::North || ForcedWall.Edge == EWallEdge::South)
		{
			// North/South walls: Get X and StartY from EdgeCells
			int32 X = EdgeCells[ForcedWall.StartCell].X;
			int32 StartY = EdgeCells[ForcedWall.StartCell].Y;
			WallPosition = CalculateNorthSouthWallPosition(X, StartY, WallLength, bIsNorthWall);
		}
		else
		{
			// East/West walls: Get StartX and Y from EdgeCells
			int32 StartX = EdgeCells[ForcedWall.StartCell].X;
			int32 Y = EdgeCells[ForcedWall.StartCell].Y;
			WallPosition = CalculateEastWestWallPosition(StartX, Y, WallLength, bIsEastWall);
		}

		// Get wall rotation
		FRotator WallRotation = GetWallRotationForEdge(ForcedWall.Edge);

		// Spawn base wall mesh
		UHierarchicalInstancedStaticMeshComponent* HISM = GetOrCreateHISM(BaseMesh);
		if (HISM)
		{
			FTransform WallTransform(WallRotation, WallPosition, FVector(1.0f));
			int32 InstanceIndex = HISM->AddInstance(WallTransform);

			UE_LOG(LogTemp, Warning, TEXT("    BASE WALL PLACED at %s (Instance %d)"),
				*WallPosition.ToString(), InstanceIndex);

			// Mark cells as occupied
			for (int32 j = 0; j < Footprint; j++)
			{
				int32 CellIndex = ForcedWall.StartCell + j;
				FIntPoint CellCoord = EdgeCells[CellIndex];
				OccupancyGrid.Add(CellCoord, EGridCellType::ECT_Wall);
			}

			// Track for Middle/Top spawning
			FWallSegmentInfo SegmentInfo;
			SegmentInfo.Edge = ForcedWall.Edge;
			SegmentInfo.StartCell = ForcedWall.StartCell;
			SegmentInfo.SegmentLength = Footprint;
			SegmentInfo.BaseTransform = WallTransform;
			SegmentInfo.BaseMesh = BaseMesh;
			SegmentInfo.WallModule = &Module;

			PlacedBaseWalls.Add(SegmentInfo);

			WallsPlaced++;
		}
		else
		{
			UE_LOG(LogTemp, Error, TEXT("    SKIPPED: Failed to get/create HISM"));
			WallsSkipped++;
		}
	}

	UE_LOG(LogTemp, Warning, TEXT("Forced walls complete: %d placed, %d skipped"), WallsPlaced, WallsSkipped);
	UE_LOG(LogTemp, Warning, TEXT("========================================"));
}

// ==================================================================================
// DOOR VARIETY HELPER FUNCTIONS (Hybrid System)
// ==================================================================================

UDoorData* AMasterRoom::SelectRandomDoorFromPool(FRandomStream& Stream) const
{
	if (!RoomData) return nullptr;
	
	// Load the DoorData asset
	UDoorData* DoorData = RoomData->DoorStyleData.LoadSynchronous();
	if (!DoorData || DoorData->DoorStylePool.Num() == 0)
	{
		// No pool available, return the DoorData itself as fallback (single door mode)
		return DoorData;
	}
	
	// Calculate total weight
	float TotalWeight = 0.0f;
	for (UDoorData* PoolDoor : DoorData->DoorStylePool)
	{
		if (PoolDoor)
		{
			TotalWeight += PoolDoor->PlacementWeight;
		}
	}
	
	if (TotalWeight <= 0.0f)
	{
		// If all weights are 0, pick randomly
		return DoorData->DoorStylePool[Stream.RandRange(0, DoorData->DoorStylePool.Num() - 1)];
	}
	
	// Weighted random selection
	float RandomValue = Stream.FRandRange(0.0f, TotalWeight);
	float CurrentWeight = 0.0f;
	
	for (UDoorData* PoolDoor : DoorData->DoorStylePool)
	{
		if (PoolDoor)
		{
			CurrentWeight += PoolDoor->PlacementWeight;
			if (RandomValue <= CurrentWeight)
			{
				return PoolDoor;
			}
		}
	}
	
	// Fallback (should never reach here)
	return DoorData->DoorStylePool[0];
}

bool AMasterRoom::CanFitDoor(EWallEdge Edge, int32 StartCell, int32 Footprint) const
{
	if (!RoomData) return false;
	
	const FIntPoint GridSize = RoomData->GridSize;
	
	// Get the edge size
	int32 EdgeSize = (Edge == EWallEdge::North || Edge == EWallEdge::South) 
		? GridSize.Y 
		: GridSize.X;
	
	// Check if door would go out of bounds
	if (StartCell < 0 || StartCell + Footprint > EdgeSize)
	{
		return false;
	}
	
	// Check for overlap with existing fixed doors
	for (const FFixedDoorLocation& ExistingDoor : FixedDoorLocations)
	{
		if (ExistingDoor.WallEdge != Edge || !ExistingDoor.DoorData)
		{
			continue;
		}
		
		int32 ExistingStart = ExistingDoor.StartCell;
		int32 ExistingEnd = ExistingStart + FMath::Max(1, ExistingDoor.DoorData->FrameFootprintY);
		
		int32 NewStart = StartCell;
		int32 NewEnd = StartCell + Footprint;
		
		// Check for overlap: [Start1, End1) overlaps [Start2, End2) if Start1 < End2 AND Start2 < End1
		if (NewStart < ExistingEnd && ExistingStart < NewEnd)
		{
			return false; // Overlap detected
		}
	}
	
	// Check OccupancyGrid to ensure door doesn't overlap with walls or other objects
	TArray<FIntPoint> EdgeCells = GetCellsForEdge(Edge);
	for (int32 i = 0; i < Footprint && (StartCell + i) < EdgeCells.Num(); ++i)
	{
		const FIntPoint& CellPos = EdgeCells[StartCell + i];
		
		// Check if this cell is already occupied by something other than a door
		if (OccupancyGrid.Contains(CellPos))
		{
			EGridCellType CellType = OccupancyGrid[CellPos];
			if (CellType != EGridCellType::ECT_Doorway && CellType != EGridCellType::ECT_Empty)
			{
				return false; // Cell is occupied by wall or other object
			}
		}
	}
	
	return true;
}

int32 AMasterRoom::GetAvailableSpaceOnEdge(EWallEdge Edge, int32 StartCell) const
{
	if (!RoomData) return 0;
	
	const FIntPoint GridSize = RoomData->GridSize;
	
	// Get the edge size
	int32 EdgeSize = (Edge == EWallEdge::North || Edge == EWallEdge::South) 
		? GridSize.Y 
		: GridSize.X;
	
	// Find the next obstacle (existing door or edge end)
	int32 AvailableSpace = EdgeSize - StartCell;
	
	for (const FFixedDoorLocation& ExistingDoor : FixedDoorLocations)
	{
		if (ExistingDoor.WallEdge != Edge || !ExistingDoor.DoorData)
		{
			continue;
		}
		
		int32 ExistingStart = ExistingDoor.StartCell;
		
		// If there's a door after our start position, limit available space
		if (ExistingStart > StartCell)
		{
			AvailableSpace = FMath::Min(AvailableSpace, ExistingStart - StartCell);
		}
	}
	
	return AvailableSpace;
}

TArray<TPair<int32, int32>> AMasterRoom::GetValidDoorLocations(EWallEdge Edge) const
{
	TArray<TPair<int32, int32>> ValidLocations;
	
	if (!RoomData) return ValidLocations;
	
	const FIntPoint GridSize = RoomData->GridSize;
	
	// Get the edge size
	int32 EdgeSize = (Edge == EWallEdge::North || Edge == EWallEdge::South) 
		? GridSize.Y 
		: GridSize.X;
	
	// Collect all existing door ranges on this edge
	TArray<TPair<int32, int32>> OccupiedRanges;
	for (const FFixedDoorLocation& ExistingDoor : FixedDoorLocations)
	{
		if (ExistingDoor.WallEdge == Edge && ExistingDoor.DoorData)
		{
			int32 Start = ExistingDoor.StartCell;
			int32 End = Start + FMath::Max(1, ExistingDoor.DoorData->FrameFootprintY);
			OccupiedRanges.Add(TPair<int32, int32>(Start, End));
		}
	}
	
	// Sort by start position
	OccupiedRanges.Sort([](const TPair<int32, int32>& A, const TPair<int32, int32>& B)
	{
		return A.Key < B.Key;
	});
	
	// Find gaps between occupied ranges
	int32 CurrentPos = 0;
	
	for (const TPair<int32, int32>& Range : OccupiedRanges)
	{
		int32 GapStart = CurrentPos;
		int32 GapEnd = Range.Key;
		
		if (GapEnd > GapStart)
		{
			// Found a gap!
			int32 GapSize = GapEnd - GapStart;
			ValidLocations.Add(TPair<int32, int32>(GapStart, GapSize));
		}
		
		CurrentPos = Range.Value;
	}
	
	// Check for gap at the end
	if (CurrentPos < EdgeSize)
	{
		int32 GapSize = EdgeSize - CurrentPos;
		ValidLocations.Add(TPair<int32, int32>(CurrentPos, GapSize));
	}
	
	return ValidLocations;
}

// --- Middle & Top Wall Spawning Implementation ---

void AMasterRoom::SpawnMiddleWalls()
{
	if (!RoomData) return;
	
	UWallData* WallData = RoomData->WallStyleData.LoadSynchronous();
	if (!WallData) return;
	
	int32 Middle1Spawned = 0;
	int32 Middle2Spawned = 0;
	int32 MiddleSkipped = 0;
	
	UE_LOG(LogTemp, Warning, TEXT("========================================"));
	UE_LOG(LogTemp, Warning, TEXT("SPAWNING MIDDLE WALLS (2-Layer System)"));
	UE_LOG(LogTemp, Warning, TEXT("Base wall segments to process: %d"), PlacedBaseWalls.Num());
	
	for (const FWallSegmentInfo& Segment : PlacedBaseWalls)
	{
		// Check if this module exists
		if (!Segment.WallModule)
		{
			MiddleSkipped++;
			continue;
		}
		
		// --- MIDDLE 1 LAYER ---
		// Try to load Middle1 mesh
		UStaticMesh* Middle1Mesh = Segment.WallModule->Middle1Mesh.LoadSynchronous();
		
		if (Middle1Mesh)
		{
			// Get socket from Base mesh
			FVector SocketLocation;
			FRotator SocketRotation;
			bool bHasSocket = GetSocketTransform(Segment.BaseMesh, FName("TopBackCenter"), SocketLocation, SocketRotation);
			
			if (!bHasSocket)
			{
				// Fallback: Base wall height is 100cm
				SocketLocation = FVector(0, 0, 100.0f);
				SocketRotation = FRotator::ZeroRotator;
			}
			
			// Calculate world transform for Middle1
			FTransform SocketTransform(SocketRotation, SocketLocation);
			FTransform Middle1WorldTransform = SocketTransform * Segment.BaseTransform;
			
			// Spawn Middle1
			UHierarchicalInstancedStaticMeshComponent* HISM1 = GetOrCreateHISM(Middle1Mesh);
			if (HISM1)
			{
				HISM1->AddInstance(Middle1WorldTransform);
				Middle1Spawned++;
				
				// --- MIDDLE 2 LAYER (only if Middle1 exists) ---
				UStaticMesh* Middle2Mesh = Segment.WallModule->Middle2Mesh.LoadSynchronous();
				
				if (Middle2Mesh)
				{
					// Get socket from Middle1 mesh
					FVector Middle2SocketLocation;
					FRotator Middle2SocketRotation;
					bool bHasMiddle1Socket = GetSocketTransform(Middle1Mesh, FName("TopBackCenter"), 
					                                            Middle2SocketLocation, Middle2SocketRotation);
					
					if (!bHasMiddle1Socket)
					{
						// Fallback: Assume Middle1 is 100cm or 200cm tall
						// Check bounds to determine
						FBoxSphereBounds Bounds = Middle1Mesh->GetBounds();
						float Middle1Height = (Bounds.BoxExtent.Z * 2.0f);
						Middle2SocketLocation = FVector(0, 0, Middle1Height);
						Middle2SocketRotation = FRotator::ZeroRotator;
						
						UE_LOG(LogTemp, Verbose, TEXT("  Middle1 mesh missing TopBackCenter, using bounds: Z=%.1f"), Middle1Height);
					}
					
					// Chain transforms: Base → Middle1 → Middle2
					FTransform Middle1SocketTransform(Middle2SocketRotation, Middle2SocketLocation);
					FTransform Middle2WorldTransform = Middle1SocketTransform * Middle1WorldTransform;
					
					// Spawn Middle2
					UHierarchicalInstancedStaticMeshComponent* HISM2 = GetOrCreateHISM(Middle2Mesh);
					if (HISM2)
					{
						HISM2->AddInstance(Middle2WorldTransform);
						Middle2Spawned++;
					}
				}
			}
		}
		else
		{
			// No Middle1 mesh, skip entirely (Middle2 requires Middle1)
			MiddleSkipped++;
		}
	}
	
	UE_LOG(LogTemp, Warning, TEXT("Middle1 walls spawned: %d"), Middle1Spawned);
	UE_LOG(LogTemp, Warning, TEXT("Middle2 walls spawned: %d"), Middle2Spawned);
	UE_LOG(LogTemp, Warning, TEXT("Middle walls skipped (no Middle1): %d"), MiddleSkipped);
	UE_LOG(LogTemp, Warning, TEXT("========================================"));
}

void AMasterRoom::SpawnTopWalls()
{
	if (!RoomData) return;
	
	UWallData* WallData = RoomData->WallStyleData.LoadSynchronous();
	if (!WallData) return;
	
	int32 TopSpawned = 0;
	int32 TopSkipped = 0;
	
	UE_LOG(LogTemp, Warning, TEXT("========================================"));
	UE_LOG(LogTemp, Warning, TEXT("SPAWNING TOP WALLS"));
	UE_LOG(LogTemp, Warning, TEXT("Base wall segments to process: %d"), PlacedBaseWalls.Num());
	
	for (const FWallSegmentInfo& Segment : PlacedBaseWalls)
	{
		// Check if this module exists
		if (!Segment.WallModule)
		{
			TopSkipped++;
			continue;
		}
		
		// Load middle meshes (may or may not exist)
		UStaticMesh* Middle1Mesh = Segment.WallModule->Middle1Mesh.LoadSynchronous();
		UStaticMesh* Middle2Mesh = Segment.WallModule->Middle2Mesh.LoadSynchronous();
		
		// Load top mesh (required)
		UStaticMesh* TopMesh = Segment.WallModule->TopMesh.LoadSynchronous();
		
		if (!TopMesh)
		{
			TopSkipped++;
			UE_LOG(LogTemp, Verbose, TEXT("  Segment: No top mesh assigned"));
			continue;
		}
		
		// Find the highest point to stack Top from (priority: Middle2 > Middle1 > Base)
		FVector SocketLocation;
		FRotator SocketRotation;
		FTransform StackBaseTransform = Segment.BaseTransform;
		UStaticMesh* StackFromMesh = nullptr;
		
		// Determine which layer Top should stack on
		if (Middle2Mesh && Middle1Mesh)
		{
			// Stack on Middle2: Base → Middle1 → Middle2 → Top
			// Calculate Middle1 position
			FVector BaseToMiddle1Socket;
			FRotator BaseToMiddle1Rot;
			bool bHasBaseSocket = GetSocketTransform(Segment.BaseMesh, FName("TopBackCenter"), 
			                                         BaseToMiddle1Socket, BaseToMiddle1Rot);
			if (!bHasBaseSocket)
			{
				BaseToMiddle1Socket = FVector(0, 0, 100.0f);
				BaseToMiddle1Rot = FRotator::ZeroRotator;
			}
			
			FTransform BaseToMiddle1(BaseToMiddle1Rot, BaseToMiddle1Socket);
			FTransform Middle1WorldTransform = BaseToMiddle1 * Segment.BaseTransform;
			
			// Calculate Middle2 position
			FVector Middle1ToMiddle2Socket;
			FRotator Middle1ToMiddle2Rot;
			bool bHasMiddle1Socket = GetSocketTransform(Middle1Mesh, FName("TopBackCenter"), 
			                                            Middle1ToMiddle2Socket, Middle1ToMiddle2Rot);
			if (!bHasMiddle1Socket)
			{
				FBoxSphereBounds Bounds = Middle1Mesh->GetBounds();
				Middle1ToMiddle2Socket = FVector(0, 0, Bounds.BoxExtent.Z * 2.0f);
				Middle1ToMiddle2Rot = FRotator::ZeroRotator;
			}
			
			FTransform Middle1ToMiddle2(Middle1ToMiddle2Rot, Middle1ToMiddle2Socket);
			FTransform Middle2WorldTransform = Middle1ToMiddle2 * Middle1WorldTransform;
			
			// Get Middle2's top socket for Top placement
			bool bHasMiddle2Socket = GetSocketTransform(Middle2Mesh, FName("TopBackCenter"), 
			                                            SocketLocation, SocketRotation);
			if (!bHasMiddle2Socket)
			{
				FBoxSphereBounds Bounds = Middle2Mesh->GetBounds();
				SocketLocation = FVector(0, 0, Bounds.BoxExtent.Z * 2.0f);
				SocketRotation = FRotator::ZeroRotator;
			}
			
			StackBaseTransform = Middle2WorldTransform;
		}
		else if (Middle1Mesh)
		{
			// Stack on Middle1: Base → Middle1 → Top
			FVector BaseToMiddle1Socket;
			FRotator BaseToMiddle1Rot;
			bool bHasBaseSocket = GetSocketTransform(Segment.BaseMesh, FName("TopBackCenter"), 
			                                         BaseToMiddle1Socket, BaseToMiddle1Rot);
			if (!bHasBaseSocket)
			{
				BaseToMiddle1Socket = FVector(0, 0, 100.0f);
				BaseToMiddle1Rot = FRotator::ZeroRotator;
			}
			
			// Get Middle1's top socket for Top placement
			bool bHasMiddle1Socket = GetSocketTransform(Middle1Mesh, FName("TopBackCenter"), 
			                                            SocketLocation, SocketRotation);
			if (!bHasMiddle1Socket)
			{
				FBoxSphereBounds Bounds = Middle1Mesh->GetBounds();
				SocketLocation = FVector(0, 0, Bounds.BoxExtent.Z * 2.0f);
				SocketRotation = FRotator::ZeroRotator;
			}
			
			FTransform BaseToMiddle1(BaseToMiddle1Rot, BaseToMiddle1Socket);
			StackBaseTransform = BaseToMiddle1 * Segment.BaseTransform;
		}
		else
		{
			// Stack directly on Base: Base → Top
			bool bHasBaseSocket = GetSocketTransform(Segment.BaseMesh, FName("TopBackCenter"), 
			                                         SocketLocation, SocketRotation);
			if (!bHasBaseSocket)
			{
				SocketLocation = FVector(0, 0, 100.0f);
				SocketRotation = FRotator::ZeroRotator;
			}
			
			StackBaseTransform = Segment.BaseTransform;
		}
		
		// Calculate final world transform for Top
		FTransform SocketTransform(SocketRotation, SocketLocation);
		FTransform TopWorldTransform = SocketTransform * StackBaseTransform;
		
		// Spawn top mesh
		UHierarchicalInstancedStaticMeshComponent* HISM = GetOrCreateHISM(TopMesh);
		if (HISM)
		{
			HISM->AddInstance(TopWorldTransform);
			TopSpawned++;
		}
	}
	
	UE_LOG(LogTemp, Warning, TEXT("Top walls spawned: %d"), TopSpawned);
	UE_LOG(LogTemp, Warning, TEXT("Top walls skipped (no mesh): %d"), TopSkipped);
	UE_LOG(LogTemp, Warning, TEXT("========================================"));
}

bool AMasterRoom::GetSocketTransform(UStaticMesh* Mesh, FName SocketName, FVector& OutLocation, FRotator& OutRotation) const
{
	if (!Mesh) return false;
	
	// Find socket in mesh
	UStaticMeshSocket* Socket = Mesh->FindSocket(SocketName);
	if (Socket)
	{
		OutLocation = Socket->RelativeLocation;
		OutRotation = Socket->RelativeRotation;
		return true;
	}
	
	return false;
}

void AMasterRoom::SpawnCorners()
{
	UE_LOG(LogTemp, Warning, TEXT("========================================"));
	UE_LOG(LogTemp, Warning, TEXT("SpawnCorners() CALLED"));
	
	if (!RoomData)
	{
		UE_LOG(LogTemp, Error, TEXT("SpawnCorners FAILED: RoomData is NULL!"));
		return;
	}
	UE_LOG(LogTemp, Warning, TEXT("RoomData valid: %s"), *RoomData->GetName());
	
	UWallData* WallData = RoomData->WallStyleData.LoadSynchronous();
	if (!WallData)
	{
		UE_LOG(LogTemp, Error, TEXT("SpawnCorners FAILED: WallData is NULL!"));
		return;
	}
	UE_LOG(LogTemp, Warning, TEXT("WallData valid: %s"), *WallData->GetName());
	
	// Attempt to load DefaultCornerMesh
	// Note: TSoftObjectPtr::IsValid() can return false even when mesh is assigned
	// So we skip the IsValid() check and directly attempt LoadSynchronous()
	UE_LOG(LogTemp, Warning, TEXT("Attempting to load DefaultCornerMesh..."));
	
	UStaticMesh* CornerMesh = WallData->DefaultCornerMesh.LoadSynchronous();
	if (!CornerMesh)
	{
		UE_LOG(LogTemp, Warning, TEXT("SpawnCorners: No DefaultCornerMesh assigned (LoadSynchronous returned NULL) - skipping corners"));
		return;
	}
	UE_LOG(LogTemp, Warning, TEXT("Corner Mesh loaded: %s"), *CornerMesh->GetName());
	
	const FIntPoint GridSize = RoomData->GridSize;
	const FVector ActorLocation = GetActorLocation();
	
	UE_LOG(LogTemp, Warning, TEXT("Grid Size: %d x %d"), GridSize.X, GridSize.Y);
	UE_LOG(LogTemp, Warning, TEXT("Actor Location: %s"), *ActorLocation.ToString());
	
	// Get HISM for corners
	UHierarchicalInstancedStaticMeshComponent* HISM = GetOrCreateHISM(CornerMesh);
	if (!HISM)
	{
		UE_LOG(LogTemp, Error, TEXT("SpawnCorners FAILED: GetOrCreateHISM returned NULL!"));
		return;
	}
	UE_LOG(LogTemp, Warning, TEXT("HISM component created/retrieved: %s"), *HISM->GetName());
	UE_LOG(LogTemp, Warning, TEXT("HISM instances BEFORE adding corners: %d"), HISM->GetInstanceCount());
	
	// Define the 4 corners with positions and rotations
	struct FCornerInfo
	{
		FVector Position;
		FRotator Rotation;
		FString Name;
	};
	
	TArray<FCornerInfo> Corners;
	
	// SouthWest Corner (0, 0) - Bottom-left, starting point (clockwise)
	Corners.Add({
		ActorLocation + FVector(0.0f, 0.0f, 0.0f),
		FRotator(0.0f, 0.0f, 0.0f),
		TEXT("SouthWest")
	});
	
	// SouthEast Corner (0, GridSize.Y) - Bottom-right (Y+ is East)
	Corners.Add({
		ActorLocation + FVector(0.0f, GridSize.Y * CELL_SIZE, 0.0f),
		FRotator(0.0f, 0.0f, 0.0f),
		TEXT("SouthEast")
	});
	
	// NorthEast Corner (GridSize.X, GridSize.Y) - Top-right (X+ is North, Y+ is East)
	Corners.Add({
		ActorLocation + FVector(GridSize.X * CELL_SIZE, GridSize.Y * CELL_SIZE, 0.0f),
		FRotator(0.0f, 0.0f, 0.0f),
		TEXT("NorthEast")
	});
	
	// NorthWest Corner (GridSize.X, 0) - Top-left (X+ is North)
	Corners.Add({
		ActorLocation + FVector(GridSize.X * CELL_SIZE, 0.0f, 0.0f),
		FRotator(0.0f, 0.0f, 0.0f),
		TEXT("NorthWest")
	});
	
	UE_LOG(LogTemp, Warning, TEXT("Corner positions calculated: %d corners"), Corners.Num());
	UE_LOG(LogTemp, Warning, TEXT("Per-corner offsets:"));
	UE_LOG(LogTemp, Warning, TEXT("  SouthWest: %s"), *WallData->SouthWestCornerOffset.ToString());
	UE_LOG(LogTemp, Warning, TEXT("  SouthEast: %s"), *WallData->SouthEastCornerOffset.ToString());
	UE_LOG(LogTemp, Warning, TEXT("  NorthEast: %s"), *WallData->NorthEastCornerOffset.ToString());
	UE_LOG(LogTemp, Warning, TEXT("  NorthWest: %s"), *WallData->NorthWestCornerOffset.ToString());
	
	// Spawn all 4 corners with individual offsets
	int32 CornersSpawned = 0;
	
	// Array of offsets matching corner order (SW, SE, NE, NW - clockwise from bottom-left)
	TArray<FVector> CornerOffsets = {
		WallData->SouthWestCornerOffset,
		WallData->SouthEastCornerOffset,
		WallData->NorthEastCornerOffset,
		WallData->NorthWestCornerOffset
	};
	
	for (int32 i = 0; i < Corners.Num(); i++)
	{
		const FCornerInfo& Corner = Corners[i];
		const FVector& Offset = CornerOffsets[i];
		
		// Apply per-corner offset from WallData
		FVector FinalPosition = Corner.Position + Offset;
		
		FTransform CornerTransform(Corner.Rotation, FinalPosition, FVector(1.0f));
		
		UE_LOG(LogTemp, Warning, TEXT("  Adding %s corner..."), *Corner.Name);
		UE_LOG(LogTemp, Warning, TEXT("    Base Position: %s"), *Corner.Position.ToString());
		UE_LOG(LogTemp, Warning, TEXT("    Applied Offset: %s"), *Offset.ToString());
		UE_LOG(LogTemp, Warning, TEXT("    Final Position: %s"), *FinalPosition.ToString());
		UE_LOG(LogTemp, Warning, TEXT("    Rotation: %.1f°"), Corner.Rotation.Yaw);
		
		int32 InstanceIndex = HISM->AddInstance(CornerTransform);
		CornersSpawned++;
		
		UE_LOG(LogTemp, Warning, TEXT("    Instance added at index: %d"), InstanceIndex);
	}
	
	UE_LOG(LogTemp, Warning, TEXT("Corners spawned: %d"), CornersSpawned);
	UE_LOG(LogTemp, Warning, TEXT("HISM instances AFTER adding corners: %d"), HISM->GetInstanceCount());
	UE_LOG(LogTemp, Warning, TEXT("========================================"));
}

// ==================================================================================
// CEILING GENERATION
// ==================================================================================

void AMasterRoom::GenerateCeiling()
{
	if (!RoomData) return;

	UCeilingData* CeilingData = RoomData->CeilingStyleData.LoadSynchronous();
	if (!CeilingData)
	{
		UE_LOG(LogTemp, Warning, TEXT("No CeilingData assigned - skipping ceiling generation"));
		return;
	}

	const FIntPoint GridSize = RoomData->GridSize;
	const FVector ActorLocation = GetActorLocation();
	const float CeilingZ = CeilingData->CeilingHeight;

	UE_LOG(LogTemp, Warning, TEXT("========================================"));
	UE_LOG(LogTemp, Warning, TEXT("GENERATING CEILING"));
	UE_LOG(LogTemp, Warning, TEXT("Grid Size: %d x %d"), GridSize.X, GridSize.Y);
	UE_LOG(LogTemp, Warning, TEXT("Ceiling Height: %.1f"), CeilingZ);

	// Create occupancy grid for ceiling
	TArray<bool> CeilingOccupied;
	CeilingOccupied.Init(false, GridSize.X * GridSize.Y);

	int32 LargeTilesPlaced = 0;
	int32 SmallTilesPlaced = 0;

	// Helper lambda to check/mark cells
	auto IsCellOccupied = [&](int32 X, int32 Y) -> bool
	{
		if (X < 0 || X >= GridSize.X || Y < 0 || Y >= GridSize.Y) return true;
		return CeilingOccupied[Y * GridSize.X + X];
	};

	auto MarkCellsOccupied = [&](int32 StartX, int32 StartY, int32 Size)
	{
		for (int32 dx = 0; dx < Size; dx++)
		{
			for (int32 dy = 0; dy < Size; dy++)
			{
				int32 X = StartX + dx;
				int32 Y = StartY + dy;
				if (X >= 0 && X < GridSize.X && Y >= 0 && Y < GridSize.Y)
				{
					CeilingOccupied[Y * GridSize.X + X] = true;
				}
			}
		}
	};

	// PASS 1: Place large tiles (400x400 = 4x4 cells)
	if (CeilingData->LargeTilePool.Num() > 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("PASS 1: Placing large tiles (400x400)"));

		// Calculate total weight for large tiles
		float TotalWeight = 0.0f;
		for (const FCeilingTile& Tile : CeilingData->LargeTilePool)
		{
			TotalWeight += Tile.PlacementWeight;
		}

		if (TotalWeight > 0.0f)
		{
			FRandomStream RandomStream(GenerationSeed);

			// Try to place 4x4 tiles in a grid pattern
			for (int32 X = 0; X <= GridSize.X - 4; X += 4)
			{
				for (int32 Y = 0; Y <= GridSize.Y - 4; Y += 4)
				{
					// Check if 4x4 area is free
					bool bCanPlace = true;
					for (int32 dx = 0; dx < 4 && bCanPlace; dx++)
					{
						for (int32 dy = 0; dy < 4 && bCanPlace; dy++)
						{
							if (IsCellOccupied(X + dx, Y + dy))
							{
								bCanPlace = false;
							}
						}
					}

					if (bCanPlace)
					{
						// Weighted random selection
						float RandomValue = RandomStream.FRand() * TotalWeight;
						float CurrentWeight = 0.0f;
						UStaticMesh* SelectedMesh = nullptr;

						for (const FCeilingTile& Tile : CeilingData->LargeTilePool)
						{
							CurrentWeight += Tile.PlacementWeight;
							if (RandomValue <= CurrentWeight)
							{
								SelectedMesh = Tile.Mesh.LoadSynchronous();
								break;
							}
						}

						if (SelectedMesh)
						{
							// Calculate center position of 4x4 area
							FVector TilePosition = ActorLocation + FVector(
								(X + 2.0f) * CELL_SIZE,  // Center at X+2
								(Y + 2.0f) * CELL_SIZE,  // Center at Y+2
								CeilingZ
							);

							UHierarchicalInstancedStaticMeshComponent* HISM = GetOrCreateHISM(SelectedMesh);
							if (HISM)
							{
								FTransform TileTransform(CeilingData->CeilingRotation, TilePosition, FVector(1.0f));
								HISM->AddInstance(TileTransform);
								MarkCellsOccupied(X, Y, 4);
								LargeTilesPlaced++;
							}
						}
					}
				}
			}
		}

		UE_LOG(LogTemp, Warning, TEXT("Large tiles placed: %d"), LargeTilesPlaced);
	}

	// PASS 2: Fill remaining cells with small tiles (100x100 = 1x1 cell)
	if (CeilingData->SmallTilePool.Num() > 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("PASS 2: Filling gaps with small tiles (100x100)"));

		// Calculate total weight for small tiles
		float TotalWeight = 0.0f;
		for (const FCeilingTile& Tile : CeilingData->SmallTilePool)
		{
			TotalWeight += Tile.PlacementWeight;
		}

		if (TotalWeight > 0.0f)
		{
			FRandomStream RandomStream(GenerationSeed + 1000);  // Different seed for variety

			for (int32 X = 0; X < GridSize.X; X++)
			{
				for (int32 Y = 0; Y < GridSize.Y; Y++)
				{
					if (!IsCellOccupied(X, Y))
					{
						// Weighted random selection
						float RandomValue = RandomStream.FRand() * TotalWeight;
						float CurrentWeight = 0.0f;
						UStaticMesh* SelectedMesh = nullptr;

						for (const FCeilingTile& Tile : CeilingData->SmallTilePool)
						{
							CurrentWeight += Tile.PlacementWeight;
							if (RandomValue <= CurrentWeight)
							{
								SelectedMesh = Tile.Mesh.LoadSynchronous();
								break;
							}
						}

						if (SelectedMesh)
						{
							FVector TilePosition = ActorLocation + FVector(
								(X + 0.5f) * CELL_SIZE,  // Center of cell
								(Y + 0.5f) * CELL_SIZE,  // Center of cell
								CeilingZ
							);

							UHierarchicalInstancedStaticMeshComponent* HISM = GetOrCreateHISM(SelectedMesh);
							if (HISM)
							{
								FTransform TileTransform(CeilingData->CeilingRotation, TilePosition, FVector(1.0f));
								HISM->AddInstance(TileTransform);
								MarkCellsOccupied(X, Y, 1);
								SmallTilesPlaced++;
							}
						}
					}
				}
			}
		}

		UE_LOG(LogTemp, Warning, TEXT("Small tiles placed: %d"), SmallTilesPlaced);
	}

	UE_LOG(LogTemp, Warning, TEXT("Ceiling generation complete!"));
	UE_LOG(LogTemp, Warning, TEXT("Total tiles: %d large + %d small = %d total"), 
		LargeTilesPlaced, SmallTilesPlaced, LargeTilesPlaced + SmallTilesPlaced);
	UE_LOG(LogTemp, Warning, TEXT("========================================"));
}

// Editor-only overrides for lifecycle management
#if WITH_EDITOR
void AMasterRoom::PostLoad()
{
	Super::PostLoad();
	// Draw the debug grid when the actor is loaded in the editor
	if (GIsEditor)
	{
		DrawDebugGrid();
	}
}
#endif