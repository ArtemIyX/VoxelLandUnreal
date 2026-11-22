#include "World/VoxelLandM4CurveValidation.h"
#include "Data/Structs/VoxelLandCanonicalWriter.h"
#include "Data/Structs/VoxelLandM4ControlSnapshot.h"
#include "Generation/VoxelLandM4CurveBake.h"
#include "Hash/xxhash.h"
#include "Data/Structs/VoxelLandM5Diagnostic.h"
#include "Generation/VoxelLandM5Budget.h"
#include "Generation/VoxelLandM5BudgetLease.h"

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

FVoxelLandM5Diagnostic VoxelLandM4CurveValidation::ValidateMetered(const FVoxelLandM4ControlSnapshot& Channel,
	TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget, TFunctionRef<bool()> IsCancelled)
{
	auto Failure = [](EVoxelLandM5ErrorCode Code, const FString& Message) {
		FVoxelLandM5Diagnostic D;
		D.Code = Code;
		D.Stage = TEXT("M5.CurveValidation");
		D.Message = Message;
		return D;
	};
	if (IsCancelled())
	{
		return Failure(EVoxelLandM5ErrorCode::Cancelled, TEXT("Curve validation cancelled"));
	}
	const int32 Keys = Channel.CurveKeys.Num();
	const int32 Samples = Channel.CookedCurve.SamplesQ8.Num();
	if (Keys < 2 || Keys > 4097 || Channel.CookedCurve.SourceKeys.Num() != Keys
		|| Samples != FVoxelLandM4CurveLut::SampleCount)
	{
		return Failure(EVoxelLandM5ErrorCode::InvalidInput, TEXT("Invalid M4 curve key or sample count"));
	}

	// Validate() and Bake() together make a bounded linear number of key and
	// sample visits. Bake advances its segment cursor at most Keys-2 times.
	// 128 units per record covers conversion, hashing, baking, LUT validation,
	// comparisons and the fixed-call bookkeeping; these costs are precharged.
	const int64 Work = 128 * (int64(Keys) + Samples + 1);
	if (!Budget->TryChargeWork(Work))
	{
		return Failure(EVoxelLandM5ErrorCode::BudgetExceeded, TEXT("Curve validation work budget exceeded"));
	}
	// Keys, hash writer, baked samples and LUT storage are simultaneously live.
	// This bounds logical payload with growth slack, not allocator/control-block
	// overhead, diagnostic strings, or the caller-owned input snapshot.
	const int64 ScratchBytes =
		4096 + 2 * int64(Keys) * sizeof(FVoxelLandM4CurveKey) + 4 * int64(Samples) * sizeof(int32);
	TSharedPtr<FVoxelLandM5BudgetLease, ESPMode::ThreadSafe> ScratchLease;
	if (!Budget->TryReserveBytes(ScratchBytes, ScratchLease))
	{
		return Failure(EVoxelLandM5ErrorCode::BudgetExceeded, TEXT("Curve validation memory budget exceeded"));
	}
	if (IsCancelled())
	{
		return Failure(EVoxelLandM5ErrorCode::Cancelled, TEXT("Curve validation cancelled"));
	}
	FString Error;
	const bool bValid = Validate(Channel, Error);
	if (IsCancelled())
	{
		return Failure(EVoxelLandM5ErrorCode::Cancelled, TEXT("Curve validation cancelled"));
	}
	if (!bValid)
	{
		return Failure(EVoxelLandM5ErrorCode::InvalidInput, Error);
	}
	return FVoxelLandM5Diagnostic();
}
