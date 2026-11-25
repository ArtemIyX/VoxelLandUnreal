#include "Generation/VoxelLandM5InfluenceSet.h"

#include "Data/Structs/VoxelLandCanonicalWriter.h"
#include "Data/Structs/VoxelLandChunkKey.h"
#include "Data/Structs/VoxelLandM5ChunkFootprint.h"
#include "Data/Structs/VoxelLandM5WorldSnapshot.h"
#include "Generation/VoxelLandM5Budget.h"
#include "Generation/VoxelLandM5BudgetLease.h"
#include "Generation/VoxelLandM5CheckedMath.h"
#include "Generation/VoxelLandM5ChunkFootprint.h"
#include "Generation/VoxelLandM5ValidatedDefinition.h"

namespace
{
int64 FloorDiv(int64 Value, int64 Divisor)
{
	const int64 Quotient = Value / Divisor, Remainder = Value % Divisor;
	return Quotient - int64(Remainder < 0);
}
int64 CeilDiv(int64 Value, int64 Divisor)
{
	const int64 Quotient = Value / Divisor, Remainder = Value % Divisor;
	return Quotient + int64(Remainder > 0);
}
FVoxelLandM5Diagnostic InfluenceError(EVoxelLandM5ErrorCode Code, const TCHAR* Message)
{
	FVoxelLandM5Diagnostic Result; Result.Code = Code; Result.Stage = TEXT("M5.Influence"); Result.Message = Message;
	return Result;
}
}

FVoxelLandM5InfluenceSet::FVoxelLandM5InfluenceSet(TVoxelLandM5ConstRef<FVoxelLandM5ValidatedDefinition> InDefinition,
	TSharedPtr<FVoxelLandM5BudgetLease, ESPMode::ThreadSafe>&& InLease)
	: Lease(MoveTemp(InLease)), Definition(MoveTemp(InDefinition)) {}
FVoxelLandM5InfluenceSet::~FVoxelLandM5InfluenceSet() = default;
bool FVoxelLandM5InfluenceSet::UsesBudget(const FVoxelLandM5Budget& Budget) const
{
	return Definition->UsesBudget(Budget) && Lease && Budget.OwnsLease(*Lease);
}
bool FVoxelLandM5InfluenceSet::CoversChunk(const FVoxelLandChunkKey& Key) const
{
	if (Key.LOD != 0) return false;
	for (int32 Axis = 0; Axis < 3; ++Axis)
		if (Key.Coordinate[Axis] < Coverage.MinChunk[Axis] || Key.Coordinate[Axis] >= Coverage.MaxChunkExclusive[Axis]) return false;
	return true;
}

FVoxelLandM5Diagnostic FVoxelLandM5InfluenceSet::BuildFinite(TVoxelLandM5ConstRef<FVoxelLandM5ValidatedDefinition> Definition,
	FVoxelLandM5BudgetRef Budget, TFunctionRef<bool()> IsCancelled, TVoxelLandM5ConstPtr<FVoxelLandM5InfluenceSet>& Out)
{
	using E = EVoxelLandM5ErrorCode;
	using namespace VoxelLand::M5::CheckedMath;
	if (IsCancelled()) return InfluenceError(E::Cancelled, TEXT("Influence construction cancelled"));
	if (!Definition->UsesBudget(*Budget)) return InfluenceError(E::InvalidInput, TEXT("Influence definition belongs to a different budget"));
	const auto& Snapshot = Definition->GetSnapshot();
	const auto& Domain = Snapshot.Base.Domain;
	const auto& Declared = Definition->GetEffectiveBounds();
	const int64 V = Snapshot.Base.VoxelSizeCm;
	if (V < 1 || V > 65536 || Snapshot.Caves.RoofThicknessCm < 0)
		return InfluenceError(E::InvalidInput, TEXT("Invalid influence lattice or roof"));
	const bool bPocketColumns = Snapshot.Caves.Pockets.bEnabled && Snapshot.Caves.Pockets.AmplitudeCm != 0;
	int64 Width = 0, Height = 0, EndX = 0, EndY = 0;
	if (!TryMultiply(Domain.CellsX, Domain.GridSpacingCm, Width) || !TryMultiply(Domain.CellsY, Domain.GridSpacingCm, Height)
		|| !TryAdd(Domain.OriginXCm, Width, EndX) || !TryAdd(Domain.OriginYCm, Height, EndY))
		return InfluenceError(E::ArithmeticOverflow, TEXT("Finite influence domain overflow"));
	const int64 DomainMin[] = {Domain.OriginXCm, Domain.OriginYCm, Snapshot.Caves.Bounds.MinInclusive.Z};
	const int64 DomainMax[] = {EndX, EndY, Snapshot.Caves.Bounds.MaxExclusive.Z - 1};
	int64 GateMin[] = {Declared.MinInclusive.X, Declared.MinInclusive.Y, Declared.MinInclusive.Z};
	int64 GateMax[] = {Declared.MaxExclusive.X, Declared.MaxExclusive.Y, Declared.MaxExclusive.Z};
	for (int32 Axis = 0; Axis < 3; ++Axis)
		if (DomainMin[Axis] < -1048576 || DomainMax[Axis] > 1048576 || DomainMin[Axis] > DomainMax[Axis])
			return InfluenceError(E::OutOfDomain, TEXT("Finite domain exceeds the proved geometry backend"));
	const int64 MaskHalo = CeilDiv(Snapshot.Caves.RoofThicknessCm, V) + 3;
	if (MaskHalo > MAX_int32) return InfluenceError(E::OutOfDomain, TEXT("Influence halo exceeds supported range"));
	const bool bSmooth = Snapshot.Base.Smooth.bEnabled && Snapshot.Base.Smooth.Strength > 0.f;
	const int64 OwnerHalo = 2;
	int64 UsableFirst[3] = {}, UsableLast[3] = {};
	if (!Definition->HasExplicitBounds())
	{
		// Derive a maximal integer-centimetre gate whose owner overlaps are
		// exactly the chunk keys with complete read dependencies. This is a
		// declaration, not proof that contributions outside it are inert.
		for (int32 Axis = 0; Axis < 3; ++Axis)
		{
			const int64 Pad = 1 + OwnerHalo + MaskHalo + int64(bSmooth)
				+ int64(Axis < 2 && Snapshot.Base.bNormalizeSlope);
			UsableFirst[Axis] = CeilDiv(CeilDiv(DomainMin[Axis], V) + Pad, 32);
			UsableLast[Axis] = FloorDiv(FloorDiv(DomainMax[Axis], V) - 32 - Pad, 32) + 1;
			int64 Lower = 0, Upper = 0;
			if (UsableFirst[Axis] >= UsableLast[Axis]
				|| !TryMultiply(UsableFirst[Axis], 32, Lower) || !TryAdd(Lower, 1 + OwnerHalo, Lower)
				|| !TryMultiply(Lower, V, Lower) || !TryAdd(Lower, 1, GateMin[Axis])
				|| !TryMultiply(UsableLast[Axis], 32, Upper) || !TrySubtract(Upper, 1 + OwnerHalo, Upper)
				|| !TryMultiply(Upper, V, GateMax[Axis]) || GateMin[Axis] >= GateMax[Axis]
				|| GateMin[Axis] < DomainMin[Axis] || GateMax[Axis] - 1 > DomainMax[Axis])
				return InfluenceError(E::OutOfDomain, TEXT("No complete usable finite gate is representable"));
		}
	}
	int64 First[3] = {}, LastExclusive[3] = {}, Total = 1;
	for (int32 Axis = 0; Axis < 3; ++Axis)
	{
		if (IsCancelled()) return InfluenceError(E::Cancelled, TEXT("Influence construction cancelled"));
		// Both default and explicit declarations cover every owner overlap.
		// Do not clip this gate to the subset with available read dependencies.
		// Inputs and V were bounded above, so these products fit int64.
		First[Axis] = CeilDiv(GateMin[Axis] - (33 + OwnerHalo) * V, 32 * V);
		LastExclusive[Axis] = FloorDiv(GateMax[Axis] - 1 + (1 + OwnerHalo) * V, 32 * V) + 1;
		const int64 Count = LastExclusive[Axis] - First[Axis];
		if (!Definition->HasExplicitBounds() && (First[Axis] != UsableFirst[Axis] || LastExclusive[Axis] != UsableLast[Axis]))
			return InfluenceError(E::Unproven, TEXT("Usable gate and owner coverage disagree"));
		if (First[Axis] < MIN_int32 || LastExclusive[Axis] > MAX_int32 || Count < 1
			|| !TryMultiplyNonNegative(Total, Count, Total) || Total > MAX_int32)
			return InfluenceError(E::OutOfDomain, TEXT("Finite influence coverage is empty or exceeds supported limits"));
	}
	FVoxelLandChunkKey FirstKey, LastKey;
	FirstKey.Coordinate = FIntVector(int32(First[0]), int32(First[1]), int32(First[2]));
	LastKey.Coordinate = FIntVector(int32(LastExclusive[0] - 1), int32(LastExclusive[1] - 1), int32(LastExclusive[2] - 1));
	FVoxelLandM5ChunkFootprint FirstFootprint, LastFootprint;
	if (!VoxelLand::M5::ChunkFootprint::TryBuildExpanded(FirstKey, V, bSmooth, Snapshot.Base.bNormalizeSlope,
		int32(MaskHalo), int32(OwnerHalo), FirstFootprint)
		|| !VoxelLand::M5::ChunkFootprint::TryBuildExpanded(LastKey, V, bSmooth, Snapshot.Base.bNormalizeSlope,
			int32(MaskHalo), int32(OwnerHalo), LastFootprint))
		return InfluenceError(E::ArithmeticOverflow, TEXT("Finite influence read footprint overflow"));
	const int64 ReadMin[] = {FirstFootprint.HeightInputMin.X, FirstFootprint.HeightInputMin.Y, FirstFootprint.DensityInputMin.Z};
	const int64 ReadMax[] = {LastFootprint.HeightInputMax.X, LastFootprint.HeightInputMax.Y, LastFootprint.DensityInputMax.Z};
	for (int32 Axis = 0; Axis < 3; ++Axis)
		if (ReadMin[Axis] < DomainMin[Axis] || ReadMax[Axis] > DomainMax[Axis])
			return InfluenceError(E::OutOfDomain, TEXT("Declared influence requires unsupported M4 read dependencies"));
	int64 PocketMinZ = 0, PocketMaxZExclusive = 0, PocketRawMinZ = 0, PocketRawMaxZExclusive = 0;
	int64 PocketXYMin[2] = {}, PocketXYMax[2] = {}, PocketRawXYMin[2] = {}, PocketRawXYMax[2] = {};
	if (bPocketColumns)
	{
		// Pocket depth tests read a 5x5 neighborhood of every owner sample;
		// the raw M4 XY stencil is a sequential smoothing and slope expansion.
		const int64 OwnerMin[] = {FirstFootprint.OwnerMin.X, FirstFootprint.OwnerMin.Y};
		const int64 OwnerMax[] = {LastFootprint.OwnerMax.X, LastFootprint.OwnerMax.Y};
		int64 Neighborhood = 0, Stencil = 0;
		if (!TryMultiply(2, V, Neighborhood)
			|| !TryMultiply(int64(bSmooth) + int64(Snapshot.Base.bNormalizeSlope), V, Stencil))
			return InfluenceError(E::ArithmeticOverflow, TEXT("Pocket XY footprint overflow"));
		for (int32 Axis = 0; Axis < 2; ++Axis)
		{
			if (!TrySubtract(OwnerMin[Axis], Neighborhood, PocketXYMin[Axis])
				|| !TryAdd(OwnerMax[Axis], Neighborhood, PocketXYMax[Axis])
				|| !TrySubtract(PocketXYMin[Axis], Stencil, PocketRawXYMin[Axis])
				|| !TryAdd(PocketXYMax[Axis], Stencil, PocketRawXYMax[Axis])
				|| PocketRawXYMin[Axis] < DomainMin[Axis] || PocketRawXYMax[Axis] > DomainMax[Axis])
				return InfluenceError(E::OutOfDomain, TEXT("Pocket XY dependencies exceed the finite M4 grid"));
		}
		// Restrict post-filter samples so their raw smoothing halo stays inside
		// the complete declared source Z. No column storage is allocated here.
		const int64 RawFirst = CeilDiv(Snapshot.Caves.Bounds.MinInclusive.Z, V);
		const int64 RawLastExclusive = CeilDiv(Snapshot.Caves.Bounds.MaxExclusive.Z, V);
		int64 ColumnFirst = 0, ColumnLastExclusive = 0, ColumnLast = 0, RawLast = 0;
		if (!TryAdd(RawFirst, int64(bSmooth), ColumnFirst) || !TrySubtract(RawLastExclusive, int64(bSmooth), ColumnLastExclusive)
			|| ColumnFirst >= ColumnLastExclusive || !TrySubtract(ColumnLastExclusive, 1, ColumnLast) || !TrySubtract(RawLastExclusive, 1, RawLast)
			|| !TryMultiply(ColumnFirst, V, PocketMinZ) || !TryMultiply(ColumnLast, V, PocketMaxZExclusive)
			|| !TryAdd(PocketMaxZExclusive, 1, PocketMaxZExclusive)
			|| !TryMultiply(RawFirst, V, PocketRawMinZ) || !TryMultiply(RawLast, V, PocketRawMaxZExclusive)
			|| !TryAdd(PocketRawMaxZExclusive, 1, PocketRawMaxZExclusive)
			|| PocketRawMinZ < -1048576 || PocketRawMaxZExclusive - 1 > 1048576)
			return InfluenceError(E::OutOfDomain, TEXT("Pocket column exceeds the proved geometry backend"));
	}
	const auto DefinitionBytes = Definition->GetCanonicalBytes();
	int64 Capacity = 0, Bytes = 0;
	if (!TryAdd(DefinitionBytes.Num(), 256, Capacity) || !TryMultiplyNonNegative(Capacity, 4, Bytes)
		|| !TryAdd(Bytes, 4096, Bytes)) return InfluenceError(E::ArithmeticOverflow, TEXT("Influence admission overflow"));
	TSharedPtr<FVoxelLandM5BudgetLease, ESPMode::ThreadSafe> Lease;
	if (!Budget->TryChargeWork(Capacity + 128) || !Budget->TryReserveBytes(Bytes, Lease))
		return InfluenceError(E::BudgetExceeded, TEXT("Influence admission exhausted"));
	FCanonicalWriter Writer; Writer.Bytes.Reserve(int32(Capacity));
	Writer.U32(0x35494c56); Writer.U32(1); Writer.U32(uint32(DefinitionBytes.Num()));
	Writer.Bytes.Append(DefinitionBytes.GetData(), DefinitionBytes.Num());
	for (int32 Axis = 0; Axis < 3; ++Axis)
	{
		Writer.U32(uint32(int32(First[Axis]))); Writer.U32(uint32(int32(LastExclusive[Axis])));
		const uint64 Values[] = {uint64(GateMin[Axis]), uint64(GateMax[Axis]), uint64(ReadMin[Axis]), uint64(ReadMax[Axis] + 1)};
		for (uint64 Value : Values) { Writer.U32(uint32(Value)); Writer.U32(uint32(Value >> 32)); }
	}
	Writer.U8(bPocketColumns ? 1 : 0);
	if (bPocketColumns)
	{
		const int64 Values[] = {Snapshot.Caves.Bounds.MinInclusive.Z, Snapshot.Caves.Bounds.MaxExclusive.Z,
			PocketXYMin[0], PocketXYMax[0] + 1, PocketXYMin[1], PocketXYMax[1] + 1,
			PocketMinZ, PocketMaxZExclusive, PocketRawXYMin[0], PocketRawXYMax[0] + 1,
			PocketRawXYMin[1], PocketRawXYMax[1] + 1, PocketRawMinZ, PocketRawMaxZExclusive};
		for (int64 Value : Values) { const uint64 Bits = uint64(Value); Writer.U32(uint32(Bits)); Writer.U32(uint32(Bits >> 32)); }
	}
	if (IsCancelled()) return InfluenceError(E::Cancelled, TEXT("Influence construction cancelled"));
	TSharedPtr<FVoxelLandM5InfluenceSet, ESPMode::ThreadSafe> Built = MakeShareable(new FVoxelLandM5InfluenceSet(Definition, MoveTemp(Lease)));
	Built->DeclaredBounds.MinInclusive.X = GateMin[0]; Built->DeclaredBounds.MinInclusive.Y = GateMin[1]; Built->DeclaredBounds.MinInclusive.Z = GateMin[2];
	Built->DeclaredBounds.MaxExclusive.X = GateMax[0]; Built->DeclaredBounds.MaxExclusive.Y = GateMax[1]; Built->DeclaredBounds.MaxExclusive.Z = GateMax[2];
	Built->Coverage.MinChunk = FirstKey.Coordinate;
	Built->Coverage.MaxChunkExclusive = FIntVector(int32(LastExclusive[0]), int32(LastExclusive[1]), int32(LastExclusive[2]));
	Built->Coverage.ReadBoundsCm.MinInclusive.X = ReadMin[0]; Built->Coverage.ReadBoundsCm.MinInclusive.Y = ReadMin[1]; Built->Coverage.ReadBoundsCm.MinInclusive.Z = ReadMin[2];
	Built->Coverage.ReadBoundsCm.MaxExclusive.X = ReadMax[0] + 1; Built->Coverage.ReadBoundsCm.MaxExclusive.Y = ReadMax[1] + 1; Built->Coverage.ReadBoundsCm.MaxExclusive.Z = ReadMax[2] + 1;
	Built->Coverage.bRequiresPocketColumns = bPocketColumns;
	if (bPocketColumns)
	{
		Built->Coverage.PocketColumnBoundsCm.MinInclusive.X = PocketXYMin[0];
		Built->Coverage.PocketColumnBoundsCm.MinInclusive.Y = PocketXYMin[1];
		Built->Coverage.PocketColumnBoundsCm.MinInclusive.Z = PocketMinZ;
		Built->Coverage.PocketColumnBoundsCm.MaxExclusive.X = PocketXYMax[0] + 1;
		Built->Coverage.PocketColumnBoundsCm.MaxExclusive.Y = PocketXYMax[1] + 1;
		Built->Coverage.PocketColumnBoundsCm.MaxExclusive.Z = PocketMaxZExclusive;
		Built->Coverage.PocketColumnReadBoundsCm = Built->Coverage.PocketColumnBoundsCm;
		Built->Coverage.PocketColumnReadBoundsCm.MinInclusive.X = PocketRawXYMin[0];
		Built->Coverage.PocketColumnReadBoundsCm.MinInclusive.Y = PocketRawXYMin[1];
		Built->Coverage.PocketColumnReadBoundsCm.MaxExclusive.X = PocketRawXYMax[0] + 1;
		Built->Coverage.PocketColumnReadBoundsCm.MaxExclusive.Y = PocketRawXYMax[1] + 1;
		Built->Coverage.PocketColumnReadBoundsCm.MinInclusive.Z = PocketRawMinZ;
		Built->Coverage.PocketColumnReadBoundsCm.MaxExclusive.Z = PocketRawMaxZExclusive;
	}
	Built->CanonicalBytes = MoveTemp(Writer.Bytes);
	if (IsCancelled()) return InfluenceError(E::Cancelled, TEXT("Influence construction cancelled"));
	Out = MoveTemp(Built);
	return {};
}
