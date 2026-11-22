#include "World/VoxelLandM4CurveValidation.h"
#include "Data/Structs/VoxelLandCanonicalWriter.h"
#include "Data/Structs/VoxelLandM4ControlSnapshot.h"
#include "Generation/VoxelLandM4CurveBake.h"
#include "Hash/xxhash.h"

bool VoxelLandM4CurveValidation::Validate(const FVoxelLandM4ControlSnapshot& Channel, FString& Error)
{
	const FVoxelLandM4CookedCurve& Cooked = Channel.CookedCurve;
	if (Cooked.BakeVersion != int32(VoxelLandM4CurveBake::AlgorithmVersion)
		|| Cooked.SamplesQ8.Num() != FVoxelLandM4CurveLut::SampleCount || Channel.CurveKeys.Num() < 2
		|| Channel.CurveKeys.Num() > 4097 || Cooked.SourceKeys.Num() != Channel.CurveKeys.Num())
	{
		Error = TEXT("M4 baked curve is missing, unsupported, or exceeds key limits");
		return false;
	}
	TArray<FVoxelLandM4CurveKey> Keys;
	Keys.Reserve(Channel.CurveKeys.Num());
	for (int32 Index = 0; Index < Channel.CurveKeys.Num(); ++Index)
	{
		const FVoxelLandM4CurveAuthoringKey& Key = Channel.CurveKeys[Index];
		const FVoxelLandM4CurveAuthoringKey& Source = Cooked.SourceKeys[Index];
		if (Key.InputQ16 < 0 || Key.InputQ16 > 65535 || Key.InputQ16 != Source.InputQ16
			|| Key.OutputQ8 != Source.OutputQ8)
		{
			Error = TEXT("M4 baked curve has invalid or stale source keys");
			return false;
		}
		FVoxelLandM4CurveKey Converted;
		Converted.InputQ16 = uint16(Key.InputQ16);
		Converted.Output = Key.OutputQ8;
		Keys.Add(Converted);
	}
	FCanonicalWriter Writer;
	for (int32 Sample : Cooked.SamplesQ8)
	{
		Writer.U32(uint32(Sample));
	}
	const uint64 Hash = FXxHash64::HashBuffer(Writer.Bytes.GetData(), Writer.Bytes.Num()).Hash;
	int64 Bits = 0;
	FMemory::Memcpy(&Bits, &Hash, sizeof(Bits));
	if (Cooked.SamplesHash != Bits)
	{
		Error = TEXT("M4 baked curve hash mismatch");
		return false;
	}
	TArray<int32> Expected;
	TSharedPtr<const FVoxelLandM4CurveLut, ESPMode::ThreadSafe> Curve;
	if (!VoxelLandM4CurveBake::Bake(Keys, -512000000, 512000000, Expected, Curve, Error))
	{
		return false;
	}
	if (Expected != Cooked.SamplesQ8)
	{
		Error = TEXT("M4 baked curve samples do not match their source keys");
		return false;
	}
	Error.Reset();
	return true;
}
