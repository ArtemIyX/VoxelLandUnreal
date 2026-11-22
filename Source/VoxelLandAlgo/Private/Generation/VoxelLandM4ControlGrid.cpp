#include "Generation/VoxelLandM4ControlGrid.h"

#include "Generation/VoxelLandGenerator.h"
#include "Generation/VoxelLandM4Control.h"
#include <limits>

namespace VoxelLandM4ControlGrid
{
namespace
{
FVoxelLandError MakeM4ControlGridError(EVoxelLandErrorCode Code, const TCHAR* Message, int32 Instruction = INDEX_NONE)
{
	FVoxelLandError Error;
	Error.Code = Code;
	Error.Message = Message;
	Error.Instruction = Instruction;
	return Error;
}

bool IsM4ControlGridOpcode(EVoxelLandOpcode Op)
{
	switch (Op)
	{
		case EVoxelLandOpcode::LoadX:
		case EVoxelLandOpcode::LoadY:
		case EVoxelLandOpcode::LoadConstant:
		case EVoxelLandOpcode::LoadFloatParameter:
		case EVoxelLandOpcode::Add:
		case EVoxelLandOpcode::Subtract:
		case EVoxelLandOpcode::Multiply:
		case EVoxelLandOpcode::Divide:
		case EVoxelLandOpcode::Min:
		case EVoxelLandOpcode::Max:
		case EVoxelLandOpcode::Abs:
		case EVoxelLandOpcode::Negate:
		case EVoxelLandOpcode::Union:
		case EVoxelLandOpcode::SubtractShape:
		case EVoxelLandOpcode::Intersect:
		case EVoxelLandOpcode::Perlin2:
		case EVoxelLandOpcode::Fbm2:
		case EVoxelLandOpcode::Value2:
		case EVoxelLandOpcode::Simplex2:
		case EVoxelLandOpcode::StoreDensity:
		case EVoxelLandOpcode::LoadIntConstant:
		case EVoxelLandOpcode::LoadBoolConstant:
		case EVoxelLandOpcode::LoadIntParameter:
		case EVoxelLandOpcode::LoadBoolParameter:
		case EVoxelLandOpcode::IntToFloat:
		case EVoxelLandOpcode::Less:
		case EVoxelLandOpcode::LessEqual:
		case EVoxelLandOpcode::Equal:
		case EVoxelLandOpcode::BoolAnd:
		case EVoxelLandOpcode::BoolOr:
		case EVoxelLandOpcode::BoolNot:
		case EVoxelLandOpcode::SelectFloat:
		case EVoxelLandOpcode::SelectInt:
		case EVoxelLandOpcode::SelectBool:
		case EVoxelLandOpcode::Power:
		case EVoxelLandOpcode::Clamp:
			return true;
		default:
			return false;
	}
}

bool M4ControlGridEnd(int64 Origin, int64 Spacing, int32 Cells, int64& End)
{
	int64 Offset = 0;
	if (!VoxelLandM4Control::LatticePositionCm(Cells, Spacing, Offset)
	    || Origin > std::numeric_limits<int64>::max() - Offset)
	{
		return false;
	}
	End = Origin + Offset;
	return true;
}
}

FVoxelLandError ValidateProgram(const FVoxelLandProgram& Program)
{
	const FVoxelLandProgramDraft& Draft = Program.GetData();
	if (Draft.SmoothFilter.bPresent || Draft.ExternalFilterIndex != INDEX_NONE || !Draft.ExternalOperators.IsEmpty())
	{
		return MakeM4ControlGridError(
			EVoxelLandErrorCode::InvalidProgram, TEXT("M4 control grid rejects filters and external providers"));
	}
	for (int32 Index = 0; Index < Draft.Instructions.Num(); ++Index)
	{
		if (!IsM4ControlGridOpcode(Draft.Instructions[Index].Op))
		{
			return MakeM4ControlGridError(
				EVoxelLandErrorCode::InvalidProgram, TEXT("M4 control grid opcode is not proven 2D"), Index);
		}
	}
	return {};
}

FVoxelLandError Evaluate(const FVoxelLandGenerator& Generator, FVoxelLandWorkerContext& Worker, int64 OriginX,
	int64 OriginY, int64 SpacingCm, int32 CellsX, int32 CellsY, TArray<uint16>& OutSamples,
	TFunctionRef<bool()> IsCancelled)
{
	if (IsInGameThread())
	{
		return MakeM4ControlGridError(
			EVoxelLandErrorCode::GameThreadNotAllowed, TEXT("M4 control grid evaluation requires a worker thread"));
	}
	constexpr int64 ExactFloatInteger = 16777216;
	if (CellsX < 1 || CellsY < 1 || CellsX > 1024 || CellsY > 1024 || SpacingCm <= 0)
	{
		return MakeM4ControlGridError(
			EVoxelLandErrorCode::InvalidParameter, TEXT("Invalid M4 control grid dimensions or spacing"));
	}
	const int64 Width = int64(CellsX) + 1;
	const int64 Count = Width * (int64(CellsY) + 1);
	if (Count > 1050625)
	{
		return MakeM4ControlGridError(
			EVoxelLandErrorCode::InvalidParameter, TEXT("M4 control grid exceeds sample limit"));
	}
	int64 EndX = 0, EndY = 0;
	if (!M4ControlGridEnd(OriginX, SpacingCm, CellsX, EndX) || !M4ControlGridEnd(OriginY, SpacingCm, CellsY, EndY)
	    || OriginX < -ExactFloatInteger || OriginY < -ExactFloatInteger || EndX > ExactFloatInteger
	    || EndY > ExactFloatInteger)
	{
		return MakeM4ControlGridError(EVoxelLandErrorCode::NonFiniteCoordinate,
			TEXT("M4 control grid coordinates exceed exact float integer range"));
	}
	if (FVoxelLandError Error = ValidateProgram(Generator.GetProgram()))
	{
		return Error;
	}
	if (IsCancelled())
	{
		return MakeM4ControlGridError(EVoxelLandErrorCode::Cancelled, TEXT("M4 control grid cancelled"));
	}
	TArray<uint16> Samples;
	Samples.Reserve(int32(Count));
	TArray<FVector3f> Positions;
	Positions.Reserve(256);
	TArray<float> Values;
	for (int64 Start = 0; Start < Count; Start += 256)
	{
		if (IsCancelled())
		{
			return MakeM4ControlGridError(EVoxelLandErrorCode::Cancelled, TEXT("M4 control grid cancelled"));
		}
		const int32 Batch = int32(FMath::Min<int64>(256, Count - Start));
		Positions.Reset();
		for (int32 Lane = 0; Lane < Batch; ++Lane)
		{
			const int64 Index = Start + Lane;
			// Validated nonnegative extents bound every coordinate and product.
			const int64 X = OriginX + (Index % Width) * SpacingCm;
			const int64 Y = OriginY + (Index / Width) * SpacingCm;
			Positions.Emplace(float(X), float(Y), 0.0f);
		}
		if (FVoxelLandError Error = Generator.EvaluateSamplesRaw(Positions, Worker, Values))
		{
			return Error;
		}
		for (float Value : Values)
		{
			uint16 Quantized = 0;
			if (!VoxelLandM4Control::Quantize(Value, Quantized))
			{
				return MakeM4ControlGridError(
					EVoxelLandErrorCode::NonFiniteDensity, TEXT("Non-finite M4 control output"));
			}
			Samples.Add(Quantized);
		}
	}
	if (IsCancelled())
	{
		return MakeM4ControlGridError(EVoxelLandErrorCode::Cancelled, TEXT("M4 control grid cancelled"));
	}
	OutSamples = MoveTemp(Samples);
	return {};
}
}