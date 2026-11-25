#pragma once

#include "CoreMinimal.h"
#include "Data/Program/Execution/VoxelLandOpcode.h"

// A/B/C are source registers. Imm selects a constant, parameter, or noise config.
// Plane uses four constants at Imm (unit normal XYZ, offset); sphere uses radius;
// box uses three positive half extents. Shapes consume A/B/C position registers.
struct FVoxelLandInstruction
{
	EVoxelLandOpcode Op = EVoxelLandOpcode::LoadX;
	uint16 Dst = 0, A = 0, B = 0, C = 0;
	// Additional source registers used by operators that need more than three inputs.
	uint16 D = 0, E = 0, F = 0;
	uint32 Imm = 0;
};
