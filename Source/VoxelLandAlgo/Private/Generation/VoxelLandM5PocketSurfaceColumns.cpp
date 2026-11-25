#include "Generation/VoxelLandM5PocketSurfaceColumns.h"
#include "Data/Structs/VoxelLandM5WorldSnapshot.h"
#include "Generation/VoxelLandGenerator.h"
#include "Generation/VoxelLandM5BoundBaseline.h"
#include "Generation/VoxelLandM5Budget.h"
#include "Generation/VoxelLandM5BudgetLease.h"
#include "Generation/VoxelLandM5CheckedMath.h"
#include "Generation/VoxelLandM5InfluenceSet.h"
#include "Generation/VoxelLandM5ValidatedDefinition.h"

namespace
{
FVoxelLandM5Diagnostic ColumnError(EVoxelLandM5ErrorCode Code, const TCHAR* Message)
{
	FVoxelLandM5Diagnostic Result; Result.Code = Code; Result.Stage = TEXT("M5.PocketColumns"); Result.Message = Message;
	return Result;
}
}
FVoxelLandM5PocketSurfaceColumns::FVoxelLandM5PocketSurfaceColumns(
	TVoxelLandM5ConstRef<FVoxelLandM5BoundBaseline> InBaseline, TVoxelLandM5ConstRef<FVoxelLandM5InfluenceSet> InInfluence,
	FVoxelLandM5BudgetRef InBudget, TSharedPtr<FVoxelLandM5BudgetLease, ESPMode::ThreadSafe>&& InLease)
	: OwnedBaseline(MoveTemp(InBaseline)), OwnedInfluence(MoveTemp(InInfluence)), OwnedBudget(MoveTemp(InBudget)), Lease(MoveTemp(InLease)) {}
FVoxelLandM5PocketSurfaceColumns::~FVoxelLandM5PocketSurfaceColumns() = default;
bool FVoxelLandM5PocketSurfaceColumns::Matches(const FVoxelLandM5BoundBaseline& InBaseline,
	const FVoxelLandM5InfluenceSet& InInfluence, const FVoxelLandM5Budget& InBudget) const
{
	return &OwnedBaseline.Get() == &InBaseline && &OwnedInfluence.Get() == &InInfluence && &OwnedBudget.Get() == &InBudget;
}
bool FVoxelLandM5PocketSurfaceColumns::GetSurfaceBounds(int64 X, int64 Y, int64& OutMinCm, int64& OutMaxCm) const
{
	int64 OffsetX = 0, OffsetY = 0;
	if (!VoxelLand::M5::CheckedMath::TrySubtract(X, TileMinX, OffsetX)
		|| !VoxelLand::M5::CheckedMath::TrySubtract(Y, TileMinY, OffsetY)
		|| OffsetX < 0 || OffsetY < 0 || OffsetX >= TileSizeX || OffsetY >= TileSizeY) return false;
	const int32 Index = int32(OffsetX + int64(TileSizeX) * OffsetY);
	OutMinCm = Lower[Index]; OutMaxCm = Upper[Index]; return true;
}
FVoxelLandM5Diagnostic FVoxelLandM5PocketSurfaceColumns::Build(
	TVoxelLandM5ConstRef<FVoxelLandM5BoundBaseline> Baseline, TVoxelLandM5ConstRef<FVoxelLandM5InfluenceSet> Influence,
	FVoxelLandWorkerContext& Worker, FVoxelLandM5PointCm OutputMinLattice, FIntPoint OutputSize,
	FVoxelLandM5BudgetRef Budget, TFunctionRef<bool()> IsCancelled, TVoxelLandM5ConstPtr<FVoxelLandM5PocketSurfaceColumns>& Out)
{
	using namespace VoxelLand::M5::CheckedMath;
	using E = EVoxelLandM5ErrorCode;
	if (IsCancelled()) return ColumnError(E::Cancelled, TEXT("Pocket column scan cancelled"));
	if (&Baseline->GetDefinition() != &Influence->GetDefinition() || !Baseline->UsesBudget(*Budget)
		|| !Influence->UsesBudget(*Budget) || !Baseline->GetGenerator().OwnsWorkerOnCurrentThread(Worker))
		return ColumnError(E::InvalidInput, TEXT("Pocket column owner or worker mismatch"));
	const auto& Coverage = Influence->GetCoverage();
	const int64 V = Influence->GetDefinition().GetSnapshot().Base.VoxelSizeCm;
	if (!Coverage.bRequiresPocketColumns || V <= 0 || OutputSize.X <= 0 || OutputSize.Y <= 0)
		return ColumnError(E::InvalidInput, TEXT("Pocket columns are not required or tile is invalid"));
	const auto& Bounds = Coverage.PocketColumnBoundsCm;
	int64 MinX = 0, MinY = 0, MaxX = 0, MaxY = 0, Xcm = 0, Ycm = 0, EndXcm = 0, EndYcm = 0;
	if (!TrySubtract(OutputMinLattice.X, 2, MinX) || !TrySubtract(OutputMinLattice.Y, 2, MinY)
		|| !TryAdd(OutputMinLattice.X, int64(OutputSize.X) + 1, MaxX)
		|| !TryAdd(OutputMinLattice.Y, int64(OutputSize.Y) + 1, MaxY)
		|| !TryMultiply(MinX, V, Xcm) || !TryMultiply(MinY, V, Ycm)
		|| !TryMultiply(MaxX, V, EndXcm) || !TryMultiply(MaxY, V, EndYcm))
		return ColumnError(E::ArithmeticOverflow, TEXT("Pocket tile bounds overflow"));
	if (Xcm < Bounds.MinInclusive.X || Ycm < Bounds.MinInclusive.Y
		|| EndXcm >= Bounds.MaxExclusive.X || EndYcm >= Bounds.MaxExclusive.Y
		|| Bounds.MinInclusive.Z % V != 0 || (Bounds.MaxExclusive.Z - 1) % V != 0)
		return ColumnError(E::OutOfDomain, TEXT("Pocket tile exceeds declared column bounds"));
	const int64 Z0 = Bounds.MinInclusive.Z / V, Z1 = (Bounds.MaxExclusive.Z - 1) / V;
	const int64 SX = int64(OutputSize.X) + 4, SY = int64(OutputSize.Y) + 4;
	int64 Columns = 0, ZCount = 0, Scan = 0, Work = 0, Bytes = 0, SlabSamples = 0, SlabBytes = 0;
	if (!TrySubtract(Z1, Z0, ZCount) || !TryAdd(ZCount, 1, ZCount)
		|| !TryMultiplyNonNegative(SX, SY, Columns) || Columns > MAX_int32 || SX > MAX_int32 || SY > MAX_int32
		|| !TryMultiplyNonNegative(Columns, ZCount, Scan) || !TryMultiplyNonNegative(Scan, 3, Work)
		|| !TryMultiplyNonNegative(Columns, 25, Scan) || !TryAdd(Work, Scan, Work)
		|| !TryMultiplyNonNegative(Columns, 25, Bytes) || !TryMultiplyNonNegative(Columns, FMath::Min<int64>(35, ZCount), SlabSamples)
		|| SlabSamples > MAX_int32 || !TryMultiplyNonNegative(SlabSamples, sizeof(int16), SlabBytes)
		|| !TryAdd(Bytes, SlabBytes, Bytes) || !TryAdd(Bytes, 4096, Bytes))
		return ColumnError(E::ArithmeticOverflow, TEXT("Pocket scan admission overflow"));
	if (!Budget->TryChargeWork(Work)) return ColumnError(E::BudgetExceeded, TEXT("Pocket scan work exhausted"));
	TSharedPtr<FVoxelLandM5BudgetLease, ESPMode::ThreadSafe> OutputLease;
	if (!Budget->TryReserveBytes(Bytes, OutputLease)) return ColumnError(E::BudgetExceeded, TEXT("Pocket scan memory exhausted"));
	TSharedRef<FVoxelLandM5PocketSurfaceColumns, ESPMode::ThreadSafe> Built =
		MakeShareable(new FVoxelLandM5PocketSurfaceColumns(Baseline, Influence, Budget, MoveTemp(OutputLease)));
	Built->TileMinX = MinX; Built->TileMinY = MinY; Built->TileSizeX = int32(SX); Built->TileSizeY = int32(SY);
	Built->Lower.Init(0, int32(Columns)); Built->Upper.Init(0, int32(Columns));
	TArray<uint8> Air; Air.Init(0, int32(Columns));
	for (int64 Offset = 0; Offset < ZCount; )
	{
		if (IsCancelled()) return ColumnError(E::Cancelled, TEXT("Pocket column scan cancelled"));
		const int32 Depth = int32(FMath::Min<int64>(35, ZCount - Offset));
		FVoxelLandM5PointCm Min; Min.X = MinX; Min.Y = MinY; Min.Z = Z0 + Offset;
		TArray<int16> Density;
		const auto Result = Baseline->GetGenerator().GenerateFiniteM4Box(Min, FIntVector(int32(SX), int32(SY), Depth),
			Worker, Budget, IsCancelled, Density);
		if (Result.Code != E::None) return Result;
		if (Density.Num() != Columns * Depth) return ColumnError(E::Unproven, TEXT("Pocket slab size mismatch"));
		for (int32 Z = 0; Z < Depth; ++Z)
			for (int32 I = 0; I < Columns; ++I)
			{
				if ((I & 1023) == 0 && IsCancelled()) return ColumnError(E::Cancelled, TEXT("Pocket column scan cancelled"));
				const int16 Q = Density[int64(Z) * Columns + I];
				if (Q == 0 || (Air[I] && Q < 0)) return ColumnError(E::Unproven, TEXT("Pocket surface is ambiguous or has multiple crossings"));
				if (Q > 0 && !Air[I])
				{
					const int64 Crossing = Z0 + Offset + Z - 1;
					if (Crossing < Z0 + 1 || Crossing + 2 > Z1)
						return ColumnError(E::Unproven, TEXT("Pocket surface lacks an enclosed vertical halo"));
					if (!TryMultiply(Crossing - 1, V, Built->Lower[I]) || !TryMultiply(Crossing + 2, V, Built->Upper[I]))
						return ColumnError(E::ArithmeticOverflow, TEXT("Pocket crossing overflow"));
					Air[I] = 1;
				}
			}
		Offset += Depth;
	}
	for (uint8 State : Air) if (!State) return ColumnError(E::Unproven, TEXT("Pocket surface lacks an enclosed vertical halo"));
	if (IsCancelled()) return ColumnError(E::Cancelled, TEXT("Pocket column scan cancelled"));
	Out = Built; return {};
}
