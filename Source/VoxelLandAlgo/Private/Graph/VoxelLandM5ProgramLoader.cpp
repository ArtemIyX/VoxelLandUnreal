#include "Graph/VoxelLandM5ProgramLoader.h"

#include "Generation/VoxelLandGenerator.h"
#include "Generation/VoxelLandM5Budget.h"
#include "Graph/VoxelLandM5DecodedDraft.h"
#include "Graph/VoxelLandM5ProgramDecoder.h"

namespace
{
FVoxelLandM5Diagnostic M5ProgramLoaderFailure(EVoxelLandM5ErrorCode Code, const TCHAR* Message)
{
	FVoxelLandM5Diagnostic Result;
	Result.Code = Code;
	Result.Stage = TEXT("M5.ProgramLoad");
	Result.Message = Message;
	return Result;
}
}

FVoxelLandM5Diagnostic FVoxelLandM5ProgramLoader::Load(const FVoxelLandGraphSnapshot& Source,
	const FVoxelLandSerializedProgram& Payload, TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget,
	TFunctionRef<bool()> IsCancelled, TSharedPtr<const FVoxelLandProgram, ESPMode::ThreadSafe>& OutProgram)
{
	if (IsCancelled())
	{
		return M5ProgramLoaderFailure(EVoxelLandM5ErrorCode::Cancelled, TEXT("Cancelled"));
	}
	if (IsInGameThread())
	{
		return M5ProgramLoaderFailure(
			EVoxelLandM5ErrorCode::InvalidInput, TEXT("Program load requires a worker thread"));
	}
	TSharedPtr<const FVoxelLandM5DecodedDraft, ESPMode::ThreadSafe> Decoded;
	if (FVoxelLandM5Diagnostic D = FVoxelLandM5ProgramDecoder::Decode(Source, Payload, Budget, IsCancelled, Decoded);
		D.Code != EVoxelLandM5ErrorCode::None)
	{
		return D;
	}
	if (!Decoded || !Decoded->UsesBudget(Budget.Get()))
	{
		return M5ProgramLoaderFailure(
			EVoxelLandM5ErrorCode::Unproven, TEXT("Decoded draft or budget provenance missing"));
	}
	if (IsCancelled())
	{
		return M5ProgramLoaderFailure(EVoxelLandM5ErrorCode::Cancelled, TEXT("Cancelled"));
	}
	TSharedPtr<const FVoxelLandProgram, ESPMode::ThreadSafe> Program;
	if (FVoxelLandM5Diagnostic D =
			FVoxelLandProgram::ValidateMetered(Decoded->GetDraft(), Budget, IsCancelled, Program);
		D.Code != EVoxelLandM5ErrorCode::None)
	{
		return D;
	}
	if (!Program)
	{
		return M5ProgramLoaderFailure(EVoxelLandM5ErrorCode::Unproven, TEXT("Validated program missing"));
	}
	if (Program->GetFingerprint() != Decoded->GetExpectedProgramFingerprint())
	{
		return M5ProgramLoaderFailure(
			EVoxelLandM5ErrorCode::InvalidInput, TEXT("Compiled program fingerprint mismatch"));
	}
	if (IsCancelled())
	{
		return M5ProgramLoaderFailure(EVoxelLandM5ErrorCode::Cancelled, TEXT("Cancelled"));
	}
	OutProgram = MoveTemp(Program);
	return {};
}
