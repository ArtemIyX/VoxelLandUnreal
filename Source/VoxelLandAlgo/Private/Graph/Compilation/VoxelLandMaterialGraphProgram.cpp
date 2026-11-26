#include "Graph/Compilation/VoxelLandMaterialGraphProgram.h"

#include "Data/Meshing/VoxelLandMaterialSignalSource.h"
#include "Generation/Terrain/ChunkLocal/VoxelLandFixedLattice.h"
#include "Graph/Compilation/VoxelLandMaterialGraphPoint.h"

class FVoxelLandMaterialGraphEvaluation final
{
public:
	FVoxelLandMaterialGraphEvaluation(const FVoxelLandMaterialGraphPoint& InPoint, const double* InValues)
		: Point(InPoint), Values(InValues)
	{
	}

	bool Scalar(const FVoxelLandMaterialGraphInstruction& Instruction, double& Out) const;

private:
	double Input(const FVoxelLandMaterialGraphInstruction& Instruction, int32 Index) const
	{
		const FVoxelLandMaterialGraphInput& Binding = Instruction.Inputs[Index];
		return Binding.Instruction == INDEX_NONE ? Binding.Default : Values[Binding.Instruction];
	}

	double Reduce(const FVoxelLandMaterialGraphInstruction& Instruction, bool bMultiply) const;
	bool Noise(const FVoxelLandMaterialGraphInstruction& Instruction, double& Out) const;
	bool Terrain(const FVoxelLandMaterialGraphInstruction& Instruction, double& Out) const;
	bool World(const FVoxelLandMaterialGraphInstruction& Instruction, double& Out) const;

	const FVoxelLandMaterialGraphPoint& Point;
	const double* Values;
};

bool FVoxelLandMaterialGraphProgram::IsSupportedNode(EVoxelLandBuiltinNode Kind)
{
	switch (Kind)
	{
		case EVoxelLandBuiltinNode::Surface:
		case EVoxelLandBuiltinNode::BlendSurfaces:
		case EVoxelLandBuiltinNode::SurfaceMaterialOutput:
		case EVoxelLandBuiltinNode::SurfaceSlope:
		case EVoxelLandBuiltinNode::TerrainSignal:
		case EVoxelLandBuiltinNode::WorldSignal:
		case EVoxelLandBuiltinNode::MaterialRemap:
		case EVoxelLandBuiltinNode::WorldNoise2D:
		case EVoxelLandBuiltinNode::PositionX:
		case EVoxelLandBuiltinNode::PositionY:
		case EVoxelLandBuiltinNode::PositionZ:
		case EVoxelLandBuiltinNode::FloatConstant:
		case EVoxelLandBuiltinNode::FloatParameter:
		case EVoxelLandBuiltinNode::IntConstant:
		case EVoxelLandBuiltinNode::IntParameter:
		case EVoxelLandBuiltinNode::BoolConstant:
		case EVoxelLandBuiltinNode::BoolParameter:
		case EVoxelLandBuiltinNode::IntToFloat:
		case EVoxelLandBuiltinNode::Add:
		case EVoxelLandBuiltinNode::Multiply:
		case EVoxelLandBuiltinNode::Subtract:
		case EVoxelLandBuiltinNode::Divide:
		case EVoxelLandBuiltinNode::Abs:
		case EVoxelLandBuiltinNode::Negate:
		case EVoxelLandBuiltinNode::Min:
		case EVoxelLandBuiltinNode::Max:
		case EVoxelLandBuiltinNode::Clamp:
		case EVoxelLandBuiltinNode::Saturate:
		case EVoxelLandBuiltinNode::Lerp:
		case EVoxelLandBuiltinNode::Less:
		case EVoxelLandBuiltinNode::LessEqual:
		case EVoxelLandBuiltinNode::Equal:
		case EVoxelLandBuiltinNode::BoolAnd:
		case EVoxelLandBuiltinNode::BoolOr:
		case EVoxelLandBuiltinNode::BoolNot:
		case EVoxelLandBuiltinNode::SelectFloat:
		case EVoxelLandBuiltinNode::SelectInt:
		case EVoxelLandBuiltinNode::SelectBool:
		case EVoxelLandBuiltinNode::RerouteFloat:
		case EVoxelLandBuiltinNode::RerouteInt:
		case EVoxelLandBuiltinNode::RerouteBool:
			return true;
		default:
			return false;
	}
}

bool FVoxelLandMaterialGraphProgram::Evaluate(const FVoxelLandMaterialGraphPoint& Point, TArrayView<double> Weights) const
{
	if (Weights.Num() < SlotCount || Instructions.IsEmpty() || Point.Position.ContainsNaN())
	{
		return false;
	}
	double Values[MaxInstructions] = {};
	const FVoxelLandMaterialGraphEvaluation Evaluation(Point, Values);
	for (int32 Index = 0; Index < Instructions.Num(); ++Index)
	{
		if (!Evaluation.Scalar(Instructions[Index], Values[Index]) || !FMath::IsFinite(Values[Index]))
		{
			return false;
		}
	}

	// Reverse accumulation keeps repeated sources and blend DAGs linear in node count.
	for (double& Weight : Weights)
	{
		Weight = 0.0;
	}
	double Contributions[MaxInstructions] = {};
	Contributions[Instructions.Num() - 1] = 1.0;
	for (int32 Index = Instructions.Num() - 1; Index >= 0; --Index)
	{
		const double Incoming = Contributions[Index];
		if (Incoming == 0.0)
		{
			continue;
		}
		const FVoxelLandMaterialGraphInstruction& Instruction = Instructions[Index];
		switch (Instruction.Kind)
		{
			case EVoxelLandBuiltinNode::SurfaceMaterialOutput:
				Contributions[Instruction.Inputs[0].Instruction] += Incoming;
				break;
			case EVoxelLandBuiltinNode::BlendSurfaces:
				Contributions[Instruction.Inputs[0].Instruction] += Incoming * (1.0 - Values[Index]);
				Contributions[Instruction.Inputs[1].Instruction] += Incoming * Values[Index];
				break;
			case EVoxelLandBuiltinNode::Surface:
				Weights[Instruction.Slot] += Incoming;
				break;
			default:
				break;
		}
	}
	return true;
}

bool FVoxelLandMaterialGraphEvaluation::Scalar(const FVoxelLandMaterialGraphInstruction& I, double& Out) const
{
	using ENode = EVoxelLandBuiltinNode;
	switch (I.Kind)
	{
		case ENode::PositionX: Out = Point.Position.X; break;
		case ENode::PositionY: Out = Point.Position.Y; break;
		case ENode::PositionZ: Out = Point.Position.Z; break;
		case ENode::SurfaceSlope: Out = Point.SlopeDegrees; break;
		case ENode::Surface:
		case ENode::SurfaceMaterialOutput: Out = 0.0; break;
		case ENode::BlendSurfaces: Out = FMath::Clamp(Input(I, 2), 0.0, 1.0); break;
		case ENode::TerrainSignal: return Terrain(I, Out);
		case ENode::WorldSignal: return World(I, Out);
		case ENode::WorldNoise2D: return Noise(I, Out);
		case ENode::FloatConstant:
		case ENode::FloatParameter:
		case ENode::IntConstant:
		case ENode::IntParameter:
		case ENode::BoolConstant:
		case ENode::BoolParameter:
		case ENode::IntToFloat:
		case ENode::RerouteFloat:
		case ENode::RerouteInt:
		case ENode::RerouteBool: Out = Input(I, 0); break;
		case ENode::Add: Out = Reduce(I, false); break;
		case ENode::Multiply: Out = Reduce(I, true); break;
		case ENode::Subtract: Out = Input(I, 0) - Input(I, 1); break;
		case ENode::Divide: Out = Input(I, 1) == 0.0 ? 0.0 : Input(I, 0) / Input(I, 1); break;
		case ENode::Abs: Out = FMath::Abs(Input(I, 0)); break;
		case ENode::Negate: Out = -Input(I, 0); break;
		case ENode::Min: Out = FMath::Min(Input(I, 0), Input(I, 1)); break;
		case ENode::Max: Out = FMath::Max(Input(I, 0), Input(I, 1)); break;
		case ENode::Clamp: Out = FMath::Clamp(Input(I, 0), Input(I, 1), Input(I, 2)); break;
		case ENode::Saturate: Out = FMath::Clamp(Input(I, 0), 0.0, 1.0); break;
		case ENode::Lerp: Out = FMath::Lerp(Input(I, 0), Input(I, 1), Input(I, 2)); break;
		case ENode::Less: Out = Input(I, 0) < Input(I, 1); break;
		case ENode::LessEqual: Out = Input(I, 0) <= Input(I, 1); break;
		case ENode::Equal: Out = Input(I, 0) == Input(I, 1); break;
		case ENode::BoolAnd: Out = Input(I, 0) != 0.0 && Input(I, 1) != 0.0; break;
		case ENode::BoolOr: Out = Input(I, 0) != 0.0 || Input(I, 1) != 0.0; break;
		case ENode::BoolNot: Out = Input(I, 0) == 0.0; break;
		case ENode::SelectFloat:
		case ENode::SelectInt:
		case ENode::SelectBool: Out = Input(I, Input(I, 0) != 0.0 ? 1 : 2); break;
		case ENode::MaterialRemap:
		{
			const double Lower = Input(I, 1);
			const double Upper = Input(I, 2);
			const double Value = Input(I, 0);
			Out = Lower == Upper ? double(Value >= Lower) : FMath::Clamp((Value - Lower) / (Upper - Lower), 0.0, 1.0);
			break;
		}
		default: return false;
	}
	return true;
}

double FVoxelLandMaterialGraphEvaluation::Reduce(const FVoxelLandMaterialGraphInstruction& Instruction, bool bMultiply) const
{
	double Result = bMultiply ? 1.0 : 0.0;
	for (int32 Index = 0; Index < Instruction.Inputs.Num(); ++Index)
	{
		const double Value = Input(Instruction, Index);
		Result = bMultiply ? Result * Value : Result + Value;
	}
	return Result;
}

bool FVoxelLandMaterialGraphEvaluation::Noise(const FVoxelLandMaterialGraphInstruction& I, double& Out) const
{
	constexpr double MaxExactCoordinate = 4503599627370496.0;
	constexpr double Q15Max = 32767.0;
	const double X = I.Inputs[0].Instruction == INDEX_NONE ? Point.Position.X : Input(I, 0);
	const double Y = I.Inputs[1].Instruction == INDEX_NONE ? Point.Position.Y : Input(I, 1);
	double Frequency = Input(I, 2);
	const double Octaves = Input(I, 3);
	const double Gain = Input(I, 4);
	const double Lacunarity = Input(I, 5);
	if (FMath::Abs(X) >= MaxExactCoordinate || FMath::Abs(Y) >= MaxExactCoordinate || Octaves < 1.0 || Octaves > 16.0
		|| Gain < 0.0 || Gain > 1.0 || Lacunarity < 1.0)
	{
		return false;
	}
	const uint64 NodeSeed = Point.Seed ^ (uint64(I.NodeId.A) << 32) ^ I.NodeId.B
		^ ((uint64(I.NodeId.C) << 32 | I.NodeId.D) * 0x9e3779b97f4a7c15ull) ^ I.SeedSalt;
	double Sum = 0.0;
	double Mass = 0.0;
	double Amplitude = 1.0;
	const int64 CellX = FMath::FloorToInt64(X);
	const int64 CellY = FMath::FloorToInt64(Y);
	for (int32 Index = 0; Index < static_cast<int32>(Octaves) && Amplitude > 0.0; ++Index)
	{
		int32 NoiseQ15 = 0;
		if (!VoxelLandM4FixedLattice::SampleShaping2D(CellX, CellY, static_cast<float>(Frequency),
				NodeSeed + uint64(Index) * 0x9e3779b97f4a7c15ull, NoiseQ15))
		{
			return false;
		}
		Sum += Amplitude * static_cast<double>(NoiseQ15);
		Mass += Amplitude;
		Amplitude *= Gain;
		Frequency *= Lacunarity;
	}
	Out = Sum / (Mass * Q15Max);
	return true;
}

bool FVoxelLandMaterialGraphEvaluation::Terrain(const FVoxelLandMaterialGraphInstruction& I, double& Out) const
{
	return Point.Signals && Point.Signals->SampleTerrain(I.SignalId, Point.Position, Out);
}

bool FVoxelLandMaterialGraphEvaluation::World(const FVoxelLandMaterialGraphInstruction& I, double& Out) const
{
	if (Point.Signals && Point.Signals->SampleWorld(I.Channel, FVector2d(Point.Position.X, Point.Position.Y), Out))
	{
		return true;
	}
	Out = Input(I, 0);
	return Input(I, 1) == 0.0;
}
