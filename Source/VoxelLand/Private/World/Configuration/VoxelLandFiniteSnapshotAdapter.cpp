#include "World/Configuration/VoxelLandFiniteSnapshotAdapter.h"

#include "Data/World/Configuration/VoxelLandFiniteWorldSnapshot.h"
#include "Data/Terrain/Worlds/VoxelLandTerrainWorldSnapshot.h"

class FVoxelLandFiniteSnapshotAdapterInternals final
{
public:
	static constexpr int64 ExactControlLimit = 16777216;
	static constexpr int64 HeightLimitQ8 = 512000000;
	static constexpr int64 MaxGridSpacingCm = 134217;

	static bool IsFiniteDomainValid(const FVoxelLandFiniteWorldSettings& Domain)
	{
		if (Domain.CellsX < 1 || Domain.CellsX > 1024 || Domain.CellsY < 1 || Domain.CellsY > 1024
			|| Domain.GridSpacingCm < 1 || Domain.GridSpacingCm > MaxGridSpacingCm
			|| Domain.OriginXCm < -ExactControlLimit || Domain.OriginXCm > ExactControlLimit
			|| Domain.OriginYCm < -ExactControlLimit || Domain.OriginYCm > ExactControlLimit
			|| Domain.GridSpacingCm > (ExactControlLimit - Domain.OriginXCm) / Domain.CellsX
			|| Domain.GridSpacingCm > (ExactControlLimit - Domain.OriginYCm) / Domain.CellsY
			|| Domain.MinZCm >= Domain.MaxZCm || (Domain.MinZCm < 0 && Domain.MaxZCm > MAX_int64 + Domain.MinZCm))
		{
			return false;
		}
		return Domain.DropletCount >= 0 && Domain.DropletCount <= 65536 && Domain.MaxSteps >= 0
			&& Domain.MaxSteps <= 128 && Domain.ThermalIterations >= 0 && Domain.ThermalIterations <= 32
			&& Domain.CapacityMaxQ8 >= 0 && Domain.CapacityMaxQ8 <= HeightLimitQ8 && Domain.TalusQ8 >= 0
			&& Domain.TalusQ8 <= HeightLimitQ8 && Domain.MaxTransferQ8 >= 0 && Domain.MaxTransferQ8 <= HeightLimitQ8
			&& Domain.ErosionSalt >= 0 && (!Domain.bGlobalErosion || Domain.ErosionStageId.IsValid());
	}
};

bool FVoxelLandFiniteSnapshotAdapter::TryConvert(
	const FVoxelLandFiniteWorldSnapshot& Source, FVoxelLandM4WorldSnapshot& Out, FString& Error)
{
	Error.Reset();
	const FVoxelLandWorldCommonSnapshot& Common = Source.Common;
	const FVoxelLandFiniteWorldSettings& Finite = Source.Finite;
	if (!FVoxelLandFiniteSnapshotAdapterInternals::IsFiniteDomainValid(Finite) || Common.VoxelSizeCm < 1
		|| Common.VoxelSizeCm > 2000000 || Common.BaseHeightQ8 < -512000000 || Common.BaseHeightQ8 > 512000000
		|| Common.HorizontalRadius < 1 || Common.HorizontalRadius > 8 || Common.VerticalRadius < 1
		|| Common.VerticalRadius > 4 || !FMath::IsFinite(Common.SmoothStrength) || Common.SmoothStrength < 0.f
		|| Common.SmoothStrength > 1.f)
	{
		Error = TEXT("Finite snapshot domain or common settings are invalid.");
		return false;
	}
	FVoxelLandM4WorldSnapshot Result;
	// Typed captures currently describe the version-one M4 and smooth formats.
	Result.FormatVersion = 1;
	Result.HeightStage = Common.HeightStage;
	Result.Continental = Common.Continental;
	Result.Mountain = Common.Mountain;
	Result.Peaks = Common.Peaks;
	Result.Seed = Common.Seed;
	Result.VoxelSizeCm = Common.VoxelSizeCm;
	Result.BaseHeightQ8 = Common.BaseHeightQ8;
	Result.Domain.Mode = EVoxelLandM4DomainMode::Finite;
	Result.Domain.bGlobalErosion = Finite.bGlobalErosion;
	Result.Domain.OriginXCm = Finite.OriginXCm;
	Result.Domain.OriginYCm = Finite.OriginYCm;
	Result.Domain.CellsX = Finite.CellsX;
	Result.Domain.CellsY = Finite.CellsY;
	Result.Domain.GridSpacingCm = Finite.GridSpacingCm;
	Result.ErosionStageId = Finite.ErosionStageId;
	Result.ErosionSalt = Finite.ErosionSalt;
	Result.DropletCount = Finite.DropletCount;
	Result.Erosion.MaxSteps = Finite.MaxSteps;
	Result.Erosion.ThermalIterations = Finite.ThermalIterations;
	Result.Erosion.CapacityMaxQ8 = Finite.CapacityMaxQ8;
	Result.Erosion.TalusQ8 = Finite.TalusQ8;
	Result.Erosion.MaxTransferQ8 = Finite.MaxTransferQ8;
	Result.bNormalizeSlope = Common.bNormalizeSlope;
	Result.Smooth.Version = 1;
	Result.Smooth.bEnabled = Common.bSmoothEnabled;
	Result.Smooth.Strength = Common.SmoothStrength;
	Result.HeightDetail = Common.HeightDetail;
	Result.bOverhangEnabled = Common.bOverhangEnabled;
	Result.OverhangAmplitudeQ8 = Common.OverhangAmplitudeQ8;
	Result.OverhangBandQ8 = Common.OverhangBandQ8;
	Result.OverhangFrequencyPerCm = Common.OverhangFrequencyPerCm;
	Result.OverhangNodeId = Common.OverhangNodeId;
	Result.OverhangSalt = Common.OverhangSalt;
	Result.HorizontalRadius = Common.HorizontalRadius;
	Result.VerticalRadius = Common.VerticalRadius;
	Result.MaterialSetupPath = Common.MaterialSetupPath;
	Out = MoveTemp(Result);
	return true;
}
