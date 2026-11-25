#include "VoxelLandPreviewCurves.h"
#include "Data/Program/Serialization/VoxelLandCanonicalWriter.h"
#include "Data/Terrain/Worlds/VoxelLandTerrainWorldSnapshot.h"
#include "Generation/Terrain/Curves/VoxelLandCurveBake.h"
#include "Hash/xxhash.h"

bool VoxelLandM4PreviewCurves::Bake(
	FVoxelLandM4WorldSnapshot& Snapshot, TFunctionRef<bool()> IsCancelled, FString& Error)
{
	Error.Reset();
	FVoxelLandM4ControlSnapshot* Controls[] = {&Snapshot.Continental, &Snapshot.Mountain, &Snapshot.Peaks};
	FVoxelLandM4CookedCurve Baked[3];
	for (int32 Channel = 0; Channel < 3; ++Channel)
	{
		if (IsCancelled())
		{
			Error = TEXT("M4 preview curve bake cancelled");
			return false;
		}
		const auto& Source = Controls[Channel]->CurveKeys;
		if (Source.Num() < 2 || Source.Num() > 4097)
		{
			Error = TEXT("M4 preview curve key count is invalid");
			return false;
		}
		TArray<FVoxelLandM4CurveKey> Keys;
		Keys.Reserve(Source.Num());
		for (const auto& Key : Source)
		{
			if (Key.InputQ16 < 0 || Key.InputQ16 > 65535)
			{
				Error = TEXT("M4 preview curve input is outside the normalized range");
				return false;
			}
			FVoxelLandM4CurveKey Item;
			Item.InputQ16 = uint16(Key.InputQ16);
			Item.Output = Key.OutputQ8;
			Keys.Add(Item);
		}
		TSharedPtr<const FVoxelLandM4CurveLut, ESPMode::ThreadSafe> Curve;
		if (!VoxelLandM4CurveBake::Bake(Keys, -512000000, 512000000, Baked[Channel].SamplesQ8, Curve, Error))
		{
			return false;
		}
		Baked[Channel].BakeVersion = int32(VoxelLandM4CurveBake::AlgorithmVersion);
		Baked[Channel].SourceKeys = Source;
		FCanonicalWriter Writer;
		for (int32 Value : Baked[Channel].SamplesQ8)
		{
			Writer.U32(uint32(Value));
		}
		const uint64 Hash = FXxHash64::HashBuffer(Writer.Bytes.GetData(), Writer.Bytes.Num()).Hash;
		FMemory::Memcpy(&Baked[Channel].SamplesHash, &Hash, sizeof(Hash));
	}
	if (IsCancelled())
	{
		Error = TEXT("M4 preview curve bake cancelled");
		return false;
	}
	for (int32 Channel = 0; Channel < 3; ++Channel)
	{
		Controls[Channel]->CookedCurve = MoveTemp(Baked[Channel]);
	}
	return true;
}
