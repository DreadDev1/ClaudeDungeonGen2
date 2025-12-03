// Fill out your copyright notice in the Description page of Project Settings.


#include "DungeonGen/Rooms/MasterRoom.h"
#include "Net/UnrealNetwork.h"
#include "DrawDebugHelpers.h" // Needed for debug drawing
#include "Data/Room/FloorData.h"
#include "Data/Room/RoomData.h"
#include "Data/Room/WallData.h"


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

	// 1. Expand all rectangular regions into individual cells
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

	// 2. Add individual forced empty cells
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
	
	switch (Edge)
	{
		case EWallEdge::North:  // X = Max, Y varies (0 to GridSize.Y-1)
			for (int32 Y = 0; Y < GridSize.Y; ++Y)
				Cells.Add(FIntPoint(GridSize.X - 1, Y));
			break;
		
		case EWallEdge::South:  // X = 0, Y varies (0 to GridSize.Y-1)
			for (int32 Y = 0; Y < GridSize.Y; ++Y)
				Cells.Add(FIntPoint(0, Y));
			break;
		
		case EWallEdge::East:   // Y = Max, X varies (0 to GridSize.X-1)
			for (int32 X = 0; X < GridSize.X; ++X)
				Cells.Add(FIntPoint(X, GridSize.Y - 1));
			break;
		
		case EWallEdge::West:   // Y = 0, X varies (0 to GridSize.X-1)
			for (int32 X = 0; X < GridSize.X; ++X)
				Cells.Add(FIntPoint(X, 0));
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
	// North/South walls extend along Y-axis
	FVector BasePosition = GetActorLocation() + FVector(
		X * CELL_SIZE,
		StartY * CELL_SIZE,
		0.0f
	);
	
	// Pivot is at BottomBackCenter (0.5 along mesh length on Y-axis)
	float HalfLength = WallMeshLength / 2.0f;
	
	FVector WallPivotOffset;
	if (bIsNorthWall)  // X = Max, faces -X (South, into room)
	{
		WallPivotOffset = FVector(
			-CELL_SIZE / 2.0f,  // Move half-cell toward room interior (-X)
			HalfLength,         // Center along Y-axis
			0.0f
		);
	}
	else  // South wall: X = 0, faces +X (North, into room)
	{
		WallPivotOffset = FVector(
			CELL_SIZE / 2.0f,   // Move half-cell toward room interior (+X)
			HalfLength,         // Center along Y-axis
			0.0f
		);
	}
	
	return BasePosition + WallPivotOffset;
}

FVector AMasterRoom::CalculateEastWestWallPosition(int32 StartX, int32 Y, float WallMeshLength, bool bIsEastWall) const
{
	// East/West walls extend along X-axis
	FVector BasePosition = GetActorLocation() + FVector(
		StartX * CELL_SIZE,
		Y * CELL_SIZE,
		0.0f
	);
	
	// Pivot is at BottomBackCenter (0.5 along mesh length on X-axis)
	float HalfLength = WallMeshLength / 2.0f;
	
	FVector WallPivotOffset;
	if (bIsEastWall)  // Y = Max, faces -Y (West, into room)
	{
		WallPivotOffset = FVector(
			HalfLength,          // Center along X-axis
			-CELL_SIZE / 2.0f,   // Move half-cell toward room interior (-Y)
			0.0f
		);
	}
	else  // West wall: Y = 0, faces +Y (East, into room)
	{
		WallPivotOffset = FVector(
			HalfLength,          // Center along X-axis
			CELL_SIZE / 2.0f,    // Move half-cell toward room interior (+Y)
			0.0f
		);
	}
	
	return BasePosition + WallPivotOffset;
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
	bool bIsSouthWall = (Edge == EWallEdge::South);
	bool bIsEastWall = (Edge == EWallEdge::East);
	bool bIsWestWall = (Edge == EWallEdge::West);
	
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
		
		// Load meshes
		UStaticMesh* BaseMesh = BestModule->BaseMesh.LoadSynchronous();
		if (!BaseMesh) break;
		
		// Calculate position based on wall edge
		FVector Position;
		float WallMeshLength = BestModule->Y_AxisFootprint * CELL_SIZE;
		
		if (bIsNorthWall || bIsSouthWall)
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
		
		// Place base mesh
		UHierarchicalInstancedStaticMeshComponent* HISM = GetOrCreateHISM(BaseMesh);
		if (HISM)
		{
			FTransform Transform(WallRotation, Position, FVector(1.0f));
			HISM->AddInstance(Transform);
		}
		
		// TODO: Stack Middle and Top meshes vertically (Phase 2)
		// For now, we only place the Base mesh
		
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
	
	// 5. Draw Door Locations (Magenta) - Shows where doors will be placed
	
	for (const FFixedDoorLocation& DoorLoc : FixedDoorLocations)
	{
		if (!DoorLoc.DoorData) continue;
		
		// Get door footprint
		int32 DoorFootprint = FMath::Max(1, FMath::RoundToInt(DoorLoc.DoorData->FrameFootprintY / CELL_SIZE));
		
		// Get cells for this edge
		TArray<FIntPoint> EdgeCells = GetCellsForEdge(DoorLoc.WallEdge);
		
		// Draw boxes for each cell the door occupies
		for (int32 i = 0; i < DoorFootprint && (DoorLoc.StartCell + i) < EdgeCells.Num(); ++i)
		{
			int32 CellIndex = DoorLoc.StartCell + i;
			if (CellIndex >= 0 && CellIndex < EdgeCells.Num())
			{
				FIntPoint Cell = EdgeCells[CellIndex];
				
				// Center of the cell
				FVector Center = ActorLocation + FVector(
					(Cell.X + 0.5f) * CELL_SIZE,
					(Cell.Y + 0.5f) * CELL_SIZE,
					60.0f // Higher than walls for visibility
				);
				
				// Size of the box (half extent)
				FVector Extent(CELL_SIZE / 2.3f, CELL_SIZE / 2.3f, 30.0f);
				
				// Magenta color for doors
				DrawDebugBox(World, Center, Extent, FQuat::Identity, FColor::Magenta, false, 5.0f, 0, 5.0f);
			}
		}
	}
	
	// 6. Draw Wall Modules (Yellow) - Shows where wall segments are placed
	
	if (RoomData && RoomData->WallStyleData)
	{
		UWallData* WallData = RoomData->WallStyleData.LoadSynchronous();
		if (WallData && WallData->AvailableWallModules.Num() > 0)
		{
			TArray<EWallEdge> Edges = {EWallEdge::North, EWallEdge::South, EWallEdge::East, EWallEdge::West};
			
			for (EWallEdge Edge : Edges)
			{
				TArray<FIntPoint> EdgeCells = GetCellsForEdge(Edge);
				if (EdgeCells.Num() == 0) continue;
				
				// Mark door-occupied cells
				TArray<bool> CellOccupied;
				CellOccupied.SetNum(EdgeCells.Num());
				for (int32 i = 0; i < EdgeCells.Num(); ++i)
				{
					CellOccupied[i] = false;
				}
				
				for (const FFixedDoorLocation& DoorLoc : FixedDoorLocations)
				{
					if (DoorLoc.WallEdge != Edge || !DoorLoc.DoorData) continue;
					
					int32 DoorFootprint = FMath::Max(1, FMath::RoundToInt(DoorLoc.DoorData->FrameFootprintY / CELL_SIZE));
					
					for (int32 i = 0; i < DoorFootprint && (DoorLoc.StartCell + i) < EdgeCells.Num(); ++i)
					{
						int32 CellIndex = DoorLoc.StartCell + i;
						if (CellIndex >= 0 && CellIndex < CellOccupied.Num())
						{
							CellOccupied[CellIndex] = true;
						}
					}
				}
				
				// Draw wall segments (non-door cells)
				for (int32 i = 0; i < CellOccupied.Num(); ++i)
				{
					if (!CellOccupied[i] && i < EdgeCells.Num())
					{
						FIntPoint Cell = EdgeCells[i];
						
						// Center of the cell
						FVector Center = ActorLocation + FVector(
							(Cell.X + 0.5f) * CELL_SIZE,
							(Cell.Y + 0.5f) * CELL_SIZE,
							50.0f // Between doors and forced empty
						);
						
						// Size of the box (half extent)
						FVector Extent(CELL_SIZE / 2.4f, CELL_SIZE / 2.4f, 28.0f);
						
						// Yellow color for walls
						DrawDebugBox(World, Center, Extent, FQuat::Identity, FColor::Yellow, false, 5.0f, 0, 4.0f);
					}
				}
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
	if (!WallData || WallData->AvailableWallModules.Num() == 0) return;

	FRandomStream RandomStream(GenerationSeed);
	const FIntPoint GridSize = RoomData->GridSize;

	// Process each wall edge independently
	TArray<EWallEdge> Edges = {EWallEdge::North, EWallEdge::South, EWallEdge::East, EWallEdge::West};
	
	for (EWallEdge Edge : Edges)
	{
		// Step 1: Get all cells along this edge
		TArray<FIntPoint> EdgeCells = GetCellsForEdge(Edge);
		if (EdgeCells.Num() == 0) continue;

		// Step 2: Mark cells occupied by doors
		TArray<bool> CellOccupied;
		CellOccupied.SetNum(EdgeCells.Num());
		for (int32 i = 0; i < EdgeCells.Num(); ++i)
		{
			CellOccupied[i] = false;
		}

		// Check for doors on this edge
		for (const FFixedDoorLocation& DoorLoc : FixedDoorLocations)
		{
			if (DoorLoc.WallEdge != Edge || !DoorLoc.DoorData) continue;

			// Get door footprint (how many cells it occupies)
			int32 DoorFootprint = FMath::Max(1, FMath::RoundToInt(DoorLoc.DoorData->FrameFootprintY / CELL_SIZE));
			
			// Mark cells as occupied by this door
			for (int32 i = 0; i < DoorFootprint && (DoorLoc.StartCell + i) < EdgeCells.Num(); ++i)
			{
				int32 CellIndex = DoorLoc.StartCell + i;
				if (CellIndex >= 0 && CellIndex < CellOccupied.Num())
				{
					CellOccupied[CellIndex] = true;
				}
			}

			// TODO: Actually place the door actor here (Phase 2)
			// For now, we just mark the cells as occupied
		}

		// Step 3: Find continuous wall segments (non-door cells)
		int32 SegmentStart = -1;
		for (int32 i = 0; i < CellOccupied.Num(); ++i)
		{
			if (!CellOccupied[i])
			{
				// Start of a new segment
				if (SegmentStart == -1)
				{
					SegmentStart = i;
				}
			}
			else
			{
				// End of a segment (door found)
				if (SegmentStart != -1)
				{
					int32 SegmentLength = i - SegmentStart;
					FillWallSegment(Edge, SegmentStart, SegmentLength, RandomStream);
					SegmentStart = -1;
				}
			}
		}

		// Handle final segment if it extends to the end
		if (SegmentStart != -1)
		{
			int32 SegmentLength = CellOccupied.Num() - SegmentStart;
			FillWallSegment(Edge, SegmentStart, SegmentLength, RandomStream);
		}
	}
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