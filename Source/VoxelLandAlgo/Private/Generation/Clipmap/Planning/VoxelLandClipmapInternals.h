#pragma once

#include "Data/Clipmap/Planning/VoxelLandClipmapLayout.h"

class FVoxelLandM7ClipmapInternals final
{
public:
	using FKey = FVoxelLandChunkKey;
	using FPriority = TPair<double, FKey>;

	static constexpr uint8 MaxPlannerLod = 5;
	static constexpr int32 MaxPlannerWork = 2000000;
	static constexpr int32 AxisCount = 3;
	static constexpr int32 ChildrenPerSplit = 8;
	static constexpr int32 ExtraLeavesPerSplit = ChildrenPerSplit - 1;

	static bool ValidateSettings(const FVoxelLandM7ClipmapSettings& Settings);
	static bool AreSettingsCompatible(const FVoxelLandM7ClipmapSettings& A, const FVoxelLandM7ClipmapSettings& B);
	static bool MakeRoots(const FVoxelLandM7ClipmapSettings& Settings, TArray<FKey>& Roots);
	static bool CanRestoreLayout(const FVoxelLandM7ClipmapLayout& Layout, const TArray<FKey>& ExpectedRoots,
		TSet<FKey>& Leaves, int32& WorkRemaining);
	static bool RetainPreviousSplits(const FVoxelLandM7ClipmapLayout& Previous, FVoxelLandM7ClipmapLayout& Layout,
		TSet<FKey>& Leaves, int32& WorkRemaining);
	static bool ApplySplitClosure(const FKey& Leaf, int32 MaxLeaves, TSet<FKey>& Leaves, TSet<FKey>& SplitParents,
		TArray<FKey>& NewLeaves, int32& WorkRemaining);

	static bool WriteOrderedLeaves(const TSet<FKey>& Leaves, FVoxelLandM7ClipmapLayout& Layout, int32& WorkRemaining);
	static bool AppendMixedFaces(const TArray<FKey>& OrderedKeys, const TSet<FKey>& Leaves,
		TArray<FVoxelLandM7ClipmapFace>& MixedFaces, int32& WorkRemaining);

	static bool ConsumeWork(int32 Units, int32& Remaining);
	static double SquaredDistanceCm(const FKey& Leaf, const FVoxelLandM7ClipmapSettings& Settings);
	static double SquaredSplitDistanceCm(const FKey& Leaf, const FVoxelLandM7ClipmapSettings& Settings, bool bUseExit);
	static FPriority MakePriority(const FKey& Key, const FVoxelLandM7ClipmapSettings& Settings);
	static bool IsHigherPriority(const FPriority& A, const FPriority& B);
	static bool KeyLess(const FKey& A, const FKey& B);

private:
	static int64 FloorDivide(int64 Value, int64 PositiveDivisor);
	static int64 BaseChunkStride(uint8 Lod);
	static FInt64Vector BaseChunkMinimum(const FKey& Key);
	static bool TryKeyContaining(const FInt64Vector& BaseChunk, uint8 Lod, FKey& OutKey);
	static FInt64Vector FaceProbe(const FKey& Key, int32 Axis, int32 Sign);
	static FKey Child(const FKey& Parent, int32 ChildIndex);
	static bool AddRootAxis(double Low, double High, double Width, uint8 Lod, int64& Lo, int64& Hi);
	static bool TryAddMixedFace(const FKey& Coarse, int32 Axis, int8 Sign, const TSet<FKey>& Leaves,
		TArray<FVoxelLandM7ClipmapFace>& MixedFaces);
};
