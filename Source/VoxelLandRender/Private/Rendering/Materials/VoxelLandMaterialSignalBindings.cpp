#include "VoxelLandMaterialSignalBindings.h"

#include "Data/Assets/Graphs/VoxelLandTerrainGraph.h"
#include "Data/Graph/Sources/VoxelLandGraphOutputs.h"
#include "Data/Terrain/Worlds/VoxelLandWorldConfig.h"
#include "Generation/Field/VoxelLandGenerator.h"
#include "Generation/Terrain/ChunkLocal/VoxelLandFixedLattice.h"
#include "Graph/Compilation/VoxelLandGraphCompiler.h"
#include "Graph/Compilation/VoxelLandMaterialGraphProgram.h"
#include "Async/Async.h"

bool FVoxelLandMaterialSignalBindings::Capture(const UVoxelLandWorldConfig& Config,
	const FVoxelLandMaterialGraphProgram& Program, FVoxelLandWorldSignalSnapshot::FPtr World,
	TSharedPtr<const IVoxelLandMaterialSignalSource, ESPMode::ThreadSafe>& Out, FString& Error)
{
	TSharedRef<FVoxelLandMaterialSignalBindings, ESPMode::ThreadSafe> Bindings =
		MakeShared<FVoxelLandMaterialSignalBindings, ESPMode::ThreadSafe>();
	Bindings->World = MoveTemp(World);
	Bindings->bUsesFixedLattice = Config.IsInfinite();
	TMap<FGuid, EVoxelLandGraphValueType> Imported;
	for (const FVoxelLandMaterialGraphInstruction& Instruction : Program.GetInstructions())
	{
		if (Instruction.Kind == EVoxelLandBuiltinNode::TerrainSignal)
		{
			const EVoxelLandGraphValueType* Type = Imported.Find(Instruction.SignalId);
			if (Type && *Type != Instruction.SignalType)
			{
				Error = TEXT("A terrain signal is bound with conflicting pin types.");
				return false;
			}
			Imported.Add(Instruction.SignalId, Instruction.SignalType);
		}
		if (Instruction.Kind == EVoxelLandBuiltinNode::WorldSignal && Instruction.Inputs[1].Default != 0.0
			&& (!Bindings->World || !Bindings->World->HasChannel(Instruction.Channel)))
		{
			Error = FString::Printf(TEXT("Required World Signal '%s' is missing (node %s)."),
				*Instruction.Channel.ToString(), *Instruction.NodeId.ToString());
			return false;
		}
	}

	if (Imported.IsEmpty())
	{
		Out = Bindings;
		return true;
	}
	constexpr int32 MaxImportedSignals = 8;
	if (Config.IsDirectDensity() || Imported.Num() > MaxImportedSignals || !Config.Graph)
	{
		Error = TEXT("Terrain Signal imports require a Height world and allow up to eight outputs.");
		return false;
	}

	FVoxelLandGraphSnapshot Source;
	if (!Config.Graph->CaptureSnapshot(Source, Error))
	{
		return false;
	}
	TArray<FVoxelLandGraphSnapshot> Projected;
	TArray<FGuid> OutputIds;
	TArray<FString> OutputLabels;
	for (const TPair<FGuid, EVoxelLandGraphValueType>& Import : Imported)
	{
		const FGuid Id = Import.Key;
		const FVoxelLandGraphNodeRecord* Output =
			Source.Nodes.FindByPredicate([Id](const FVoxelLandGraphNodeRecord& Node) { return Node.Id == Id; });
		const EVoxelLandBuiltinNode Expected =
			Import.Value == EVoxelLandGraphValueType::Int32  ? EVoxelLandBuiltinNode::IntOutput
			: Import.Value == EVoxelLandGraphValueType::Bool ? EVoxelLandBuiltinNode::BoolOutput
															 : EVoxelLandBuiltinNode::FloatOutput;
		if (!Output || Output->OperatorId != VoxelLandBuiltinNodeId(Expected) || Output->Pins.Num() != 1
			|| Output->Pins[0].Type != Import.Value)
		{
			Error = FString::Printf(TEXT("Terrain signal %s in %s is missing or has a different type."), *Id.ToString(),
				*Config.Graph->GetPathName());
			return false;
		}
		if (Import.Value == EVoxelLandGraphValueType::Int32)
		{
			Bindings->IntegerOutputs.Add(Id);
		}
		FVoxelLandGraphSnapshot Copy = Source;
		if (!FVoxelLandGraphOutputs::Project(Copy, Id, Import.Value != EVoxelLandGraphValueType::Float, Error))
		{
			Error = FString::Printf(
				TEXT("Terrain signal %s in %s: %s"), *Id.ToString(), *Config.Graph->GetPathName(), *Error);
			return false;
		}
		Projected.Add(MoveTemp(Copy));
		OutputIds.Add(Id);
		OutputLabels.Add(Config.Graph->GetPathName() + TEXT(" / ") + Id.ToString());
	}

	FVoxelLandGeneratorSettings Settings;
	Settings.WorldSeed = static_cast<uint64>(Config.Seed);
	for (const FVoxelLandGraphParameter& Parameter : Source.Parameters)
	{
		for (const FVoxelLandParameterOverride& Override : Config.Overrides)
		{
			if (Parameter.Id != Override.ParameterId || Parameter.Type != Override.Type)
			{
				continue;
			}
			if (Override.Type == EVoxelLandGraphValueType::Float)
			{
				Settings.Profile.FloatOverrides.Add(Parameter.Name, Override.FloatValue);
			}
			if (Override.Type == EVoxelLandGraphValueType::Int32)
			{
				Settings.Profile.IntOverrides.Add(Parameter.Name, Override.IntValue);
			}
			if (Override.Type == EVoxelLandGraphValueType::Bool)
			{
				Settings.Profile.BoolOverrides.Add(Parameter.Name, Override.BoolValue);
			}
		}
	}
	const FString BuildError = Async(EAsyncExecution::ThreadPool,
		[Bindings, Projected = MoveTemp(Projected), OutputIds = MoveTemp(OutputIds),
			OutputLabels = MoveTemp(OutputLabels), Settings = MoveTemp(Settings)]() mutable -> FString {
			for (int32 Index = 0; Index < Projected.Num(); ++Index)
			{
				FVoxelLandProgramDraft Draft;
				FString CompileError;
				if (!FVoxelLandGraphCompiler::Compile(Projected[Index], Draft, CompileError))
				{
					return OutputLabels[Index] + TEXT(": ") + CompileError;
				}
				if (const FVoxelLandError Failure = FVoxelLandProgram::Validate(Draft, Settings.Program))
				{
					return OutputLabels[Index] + TEXT(": ") + Failure.Message;
				}
				TSharedPtr<const FVoxelLandGenerator, ESPMode::ThreadSafe> Generator;
				if (const FVoxelLandError Failure = FVoxelLandGenerator::Create(Settings, Generator))
				{
					return OutputLabels[Index] + TEXT(": ") + Failure.Message;
				}
				if (Bindings->bUsesFixedLattice)
				{
					if (const FVoxelLandError Failure = VoxelLandM4FixedLattice::Validate(*Generator))
					{
						return OutputLabels[Index] + TEXT(": ") + Failure.Message;
					}
				}
				Bindings->Terrain.Add(OutputIds[Index], MoveTemp(Generator));
			}
			return FString();
		}).Get();
	if (!BuildError.IsEmpty())
	{
		Error = BuildError;
		return false;
	}
	Out = Bindings;
	return true;
}

bool FVoxelLandMaterialSignalBindings::SampleTerrain(FGuid OutputId, const FVector3d& Position, double& Out) const
{
	const TSharedPtr<const FVoxelLandGenerator, ESPMode::ThreadSafe>* Generator = Terrain.Find(OutputId);
	if (!Generator || Position.ContainsNaN())
	{
		return false;
	}
	if (bUsesFixedLattice)
	{
		thread_local TArray<int64> Registers;
		return !VoxelLandM4FixedLattice::EvaluateValidatedSignedValue(
			**Generator, FMath::FloorToInt64(Position.X), FMath::FloorToInt64(Position.Y), Registers, Out);
	}

	using FWorkerSlot =
		TPair<TWeakPtr<const FVoxelLandGenerator, ESPMode::ThreadSafe>, TUniquePtr<FVoxelLandWorkerContext>>;
	thread_local TMap<const FVoxelLandGenerator*, FWorkerSlot> Workers;
	const FVoxelLandGenerator* Key = Generator->Get();
	if (!Workers.Contains(Key) && Workers.Num() >= 8)
	{
		Workers.Reset();
	}
	FWorkerSlot& Slot = Workers.FindOrAdd(Key);
	if (Slot.Key.Pin() != *Generator)
	{
		Slot.Value.Reset();
		Slot.Key = *Generator;
	}
	TUniquePtr<FVoxelLandWorkerContext>& Worker = Slot.Value;
	if (!Worker && (*Generator)->CreateWorker(Worker))
	{
		return false;
	}
	thread_local TArray<float> Values;
	const FVector3f XY(static_cast<float>(Position.X), static_cast<float>(Position.Y), 0.f);
	if (IntegerOutputs.Contains(OutputId))
	{
		thread_local TArray<int32> Integers;
		if ((*Generator)->EvaluateIntegerSamples(MakeArrayView(&XY, 1), *Worker, Integers))
		{
			Workers.Remove(Key);
			return false;
		}
		Out = Integers[0];
		return true;
	}
	if ((*Generator)->EvaluateSamplesRaw(MakeArrayView(&XY, 1), *Worker, Values))
	{
		Workers.Remove(Key);
		return false;
	}
	Out = Values[0];
	return true;
}

bool FVoxelLandMaterialSignalBindings::SampleWorld(FName Channel, const FVector2d& Position, double& Out) const
{
	return World && World->Sample(Channel, Position, Out);
}
