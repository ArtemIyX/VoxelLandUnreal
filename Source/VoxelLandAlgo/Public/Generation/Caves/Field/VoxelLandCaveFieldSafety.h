#pragma once

#include "CoreMinimal.h"
#include "Data/Caves/Infrastructure/VoxelLandCaveDiagnostic.h"
#include "Templates/Function.h"

namespace VoxelLand::M5::FieldSafety
{
// Tests sample equality in a conservative neighborhood around supplied cells;
// it is NOT a complete roof, entrance, surface, mesh, or walkability proof.
// The caller must conservatively construct the masks from the actual final
// surface and entrance geometry and provide the complete mesher footprint.
//
// CellCounts describes a non-empty, X-fastest rectangular grid. Each mask
// contains exactly X*Y*Z bytes, each 0 or 1. Baseline and Final are X-fastest
// sample grids of (X+3)*(Y+3)*(Z+3), including one sample of halo at each end.
// Cell (0,0,0)'s lower corner is sample (1,1,1); for each protected cell all
// samples of its 3x3x3 neighboring cells are compared (a 4x4x4 sample box).
// This covers cell boundary and edge ownership only when the caller supplies
// the complete necessary halo and identical mesher inputs across chunks.
//
// Explicit protection always wins. EntranceCells can exempt a roof cell only
// when the entire cell is inside the certified authorized opening. Non-exempt
// neighboring cells still compare their full contributing sample boxes, so an
// entrance cannot silently relax neighboring protection. MaxComparisons bounds
// the worst-case 64 comparisons per cell before scanning begins. Cancellation
// and all failures return a diagnostic; no caller-owned input is modified.
VOXELLANDALGO_API FVoxelLandM5Diagnostic CheckUnchangedProtectedCells(FIntVector CellCounts,
	TConstArrayView<int16> Baseline, TConstArrayView<int16> Final, TConstArrayView<uint8> ProtectedCells,
	TConstArrayView<uint8> RoofCells, TConstArrayView<uint8> EntranceCells, int64 MaxComparisons,
	TFunctionRef<bool()> IsCancelled);
}
