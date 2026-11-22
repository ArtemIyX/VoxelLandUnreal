#include "Graph/VoxelLandM4InspectionReport.h"
#include "Data/Structs/VoxelLandM4WorldSnapshot.h"

FString VoxelLandM4Inspection::DescribeSnapshot(const FVoxelLandM4WorldSnapshot& S)
{
	const auto& D = S.HeightDetail;
	return FString::Printf(
		TEXT(
			"Snapshot: format=%d; seed=%lld; source voxel=%d cm; base=%d Q8 cm; mode=%d; domain origin=(%lld,%lld), cells=(%d,%d), spacing=%lld cm; slope=%d; smoothing=(version %u, enabled %d, strength %.9g); erosion=(enabled %d, stage %s, salt %d, droplets %d, steps %d, thermal %d, capacity %lld, talus %lld, transfer %lld); height detail=(version %d, enabled %d, amplitude %d Q8 cm, frequency %.9g /cm, node %s, salt %u); overhang=(enabled %d, amplitude %d Q8 cm, band %d Q8 cm, frequency %.9g /cm, node %s, salt %d); configured preview radii=(%d,%d); material=%s\n"),
		S.FormatVersion, S.Seed, S.VoxelSizeCm, S.BaseHeightQ8, int32(S.Domain.Mode), S.Domain.OriginXCm,
		S.Domain.OriginYCm, S.Domain.CellsX, S.Domain.CellsY, S.Domain.GridSpacingCm, int32(S.bNormalizeSlope),
		S.Smooth.Version, int32(S.Smooth.bEnabled), double(S.Smooth.Strength), int32(S.Domain.bGlobalErosion),
		*S.ErosionStageId.ToString(), S.ErosionSalt, S.DropletCount, S.Erosion.MaxSteps, S.Erosion.ThermalIterations,
		S.Erosion.CapacityMaxQ8, S.Erosion.TalusQ8, S.Erosion.MaxTransferQ8, D.AlgorithmVersion, int32(D.bEnabled),
		D.AmplitudeQ8, double(D.FrequencyPerCm), *D.NodeId.ToString(), D.Salt, int32(S.bOverhangEnabled),
		S.OverhangAmplitudeQ8, S.OverhangBandQ8, double(S.OverhangFrequencyPerCm), *S.OverhangNodeId.ToString(),
		S.OverhangSalt, S.HorizontalRadius, S.VerticalRadius, *S.MaterialSetupPath.ToString());
}
