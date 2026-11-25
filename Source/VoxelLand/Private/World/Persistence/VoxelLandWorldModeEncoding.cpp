#include "World/Persistence/VoxelLandWorldModeEncoding.h"

#include "Data/Program/Serialization/VoxelLandCanonicalWriter.h"
#include "Data/World/Configuration/VoxelLandWorldSourceSnapshot.h"
#include "World/Caves/Persistence/VoxelLandCaveCanonicalSettings.h"

class FVoxelLandWorldModeEncodingInternals final
{
public:
	static void WriteU64(FCanonicalWriter& Writer, uint64 Value)
	{
		Writer.U32(uint32(Value));
		Writer.U32(uint32(Value >> 32));
	}

	static void WriteGuid(FCanonicalWriter& Writer, const FGuid& Value)
	{
		Writer.U32(Value.A);
		Writer.U32(Value.B);
		Writer.U32(Value.C);
		Writer.U32(Value.D);
	}
};

void VoxelLandWorldModeEncoding::Write(FCanonicalWriter& Writer, const FVoxelLandWorldSourceSnapshot& Source)
{
	Writer.U8('V');
	Writer.U8('W');
	Writer.U8('M');
	Writer.U8('D');
	Writer.U32(1);
	Writer.U8(uint8(Source.GetWorldKind()));
	if (const FVoxelLandFiniteWorldSnapshot* Finite = Source.GetFinite())
	{
		const FVoxelLandFiniteWorldSettings& Settings = Finite->Finite;
		FVoxelLandWorldModeEncodingInternals::WriteU64(Writer, uint64(Settings.OriginXCm));
		FVoxelLandWorldModeEncodingInternals::WriteU64(Writer, uint64(Settings.OriginYCm));
		Writer.U32(uint32(Settings.CellsX));
		Writer.U32(uint32(Settings.CellsY));
		FVoxelLandWorldModeEncodingInternals::WriteU64(Writer, uint64(Settings.GridSpacingCm));
		FVoxelLandWorldModeEncodingInternals::WriteU64(Writer, uint64(Settings.MinZCm));
		FVoxelLandWorldModeEncodingInternals::WriteU64(Writer, uint64(Settings.MaxZCm));
		Writer.U8(Settings.bGlobalErosion ? 1 : 0);
		FVoxelLandWorldModeEncodingInternals::WriteGuid(Writer, Settings.ErosionStageId);
		Writer.U32(uint32(Settings.ErosionSalt));
		Writer.U32(uint32(Settings.DropletCount));
		Writer.U32(uint32(Settings.MaxSteps));
		Writer.U32(uint32(Settings.ThermalIterations));
		FVoxelLandWorldModeEncodingInternals::WriteU64(Writer, uint64(Settings.CapacityMaxQ8));
		FVoxelLandWorldModeEncodingInternals::WriteU64(Writer, uint64(Settings.TalusQ8));
		FVoxelLandWorldModeEncodingInternals::WriteU64(Writer, uint64(Settings.MaxTransferQ8));
	}
	else
	{
		const FVoxelLandInfiniteWorldSettings& Settings = Source.GetInfinite()->Infinite;
		FVoxelLandWorldModeEncodingInternals::WriteU64(Writer, uint64(Settings.RegionSizeCm));
		FVoxelLandWorldModeEncodingInternals::WriteU64(Writer, uint64(Settings.RegionOriginXCm));
		FVoxelLandWorldModeEncodingInternals::WriteU64(Writer, uint64(Settings.RegionOriginYCm));
		FVoxelLandWorldModeEncodingInternals::WriteU64(Writer, uint64(Settings.MinZCm));
		FVoxelLandWorldModeEncodingInternals::WriteU64(Writer, uint64(Settings.MaxZCm));
	}
	const FVoxelLandWorldCommonSnapshot& Common = Source.GetCommon();
	FVoxelLandWorldModeEncodingInternals::WriteU64(Writer, uint64(Common.Seed));
	Writer.U32(uint32(Common.VoxelSizeCm));
	Writer.U32(uint32(Common.BaseHeightQ8));
	Writer.U32(uint32(Common.HeightStage.Version));
	FVoxelLandWorldModeEncodingInternals::WriteGuid(Writer, Common.HeightStage.OutputNodeId);
	FVoxelLandWorldModeEncodingInternals::WriteGuid(Writer, Common.HeightStage.ContinentalInputId);
	FVoxelLandWorldModeEncodingInternals::WriteGuid(Writer, Common.HeightStage.MountainInputId);
	FVoxelLandWorldModeEncodingInternals::WriteGuid(Writer, Common.HeightStage.PeaksInputId);
	Writer.U8(Common.bNormalizeSlope ? 1 : 0);
	Writer.U8(Common.bSmoothEnabled ? 1 : 0);
	Writer.Float(Common.SmoothStrength);
	Writer.U8(Common.HeightDetail.bEnabled ? 1 : 0);
	Writer.U32(uint32(Common.HeightDetail.AlgorithmVersion));
	Writer.U32(uint32(Common.HeightDetail.AmplitudeQ8));
	Writer.Float(Common.HeightDetail.FrequencyPerCm);
	FVoxelLandWorldModeEncodingInternals::WriteGuid(Writer, Common.HeightDetail.NodeId);
	Writer.U32(Common.HeightDetail.Salt);
	Writer.U8(Common.bOverhangEnabled ? 1 : 0);
	Writer.U32(uint32(Common.OverhangAmplitudeQ8));
	Writer.U32(uint32(Common.OverhangBandQ8));
	Writer.Float(Common.OverhangFrequencyPerCm);
	FVoxelLandWorldModeEncodingInternals::WriteGuid(Writer, Common.OverhangNodeId);
	Writer.U32(uint32(Common.OverhangSalt));
	VoxelLandM5CanonicalSettings::Write(Writer, Common.Caves);
}
