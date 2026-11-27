#include "Graph/Compilation/VoxelLandMaterialSignalBindings.h"

#include "Data/Assets/Graphs/VoxelLandTerrainGraph.h"
#include "Data/Graph/Sources/VoxelLandGraphOutputs.h"
#include "Data/Terrain/Worlds/VoxelLandWorldConfig.h"
#include "Generation/Field/VoxelLandGenerator.h"
#include "Generation/Terrain/ChunkLocal/VoxelLandFixedLattice.h"
#include "Graph/Compilation/VoxelLandGraphCompiler.h"
#include "Graph/Compilation/VoxelLandMaterialGraphProgram.h"
#include "Graph/Compilation/VoxelLandTerrainSignalCapture.h"
#include "Async/Async.h"

bool FVoxelLandMaterialSignalBindings::Capture(const UVoxelLandWorldConfig& Config,
	const FVoxelLandMaterialGraphProgram& Program, FVoxelLandWorldSignalSnapshot::FPtr World,
	TSharedPtr<const IVoxelLandMaterialSignalSource, ESPMode::ThreadSafe>& Out, FString& Error)
{
	TSharedRef<FVoxelLandMaterialSignalBindings, ESPMode::ThreadSafe> Bindings =
		MakeShared<FVoxelLandMaterialSignalBindings, ESPMode::ThreadSafe>();
	Bindings->World = MoveTemp(World);
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

	if (!Imported.IsEmpty())
	{
		FVoxelLandTerrainSignalCapture Source;
		if (Config.IsDirectDensity())
		{
			Error = TEXT("Terrain Signal imports require a Height world for Material Graphs.");
			return false;
		}
		if (!CaptureTerrain(Config, Source, Error))
		{
			return false;
		}
		const FString BuildError =
			Async(EAsyncExecution::ThreadPool, [Bindings, Source = MoveTemp(Source), Imported = MoveTemp(Imported)]() {
				FString Diagnostic;
				return Bindings->Build(Source, Imported, Diagnostic) ? FString() : Diagnostic;
			}).Get();
		if (!BuildError.IsEmpty())
		{
			Error = BuildError;
			return false;
		}
	}
	Out = Bindings;
	return true;
}

bool FVoxelLandMaterialSignalBindings::CaptureTerrain(
	const UVoxelLandWorldConfig& Config, FVoxelLandTerrainSignalCapture& Out, FString& Error)
{
	check(IsInGameThread());
	if (!Config.Graph)
	{
		Error = TEXT("Terrain signal imports require a Terrain Graph.");
		return false;
	}
	Out = {};
	FVoxelLandGraphSnapshot& Source = Out.Graph;
	if (!Config.Graph->CaptureSnapshot(Source, Error))
	{
		return false;
	}
	Out.GraphPath = Config.Graph->GetPathName();
	Out.bUseFixedLattice = Config.IsInfinite() && !Config.IsDirectDensity();
	Out.bUseThreeDimensions = Config.IsDirectDensity();
	FVoxelLandGeneratorSettings& Settings = Out.Settings;
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
	return true;
}

bool FVoxelLandMaterialSignalBindings::BuildTerrain(const FVoxelLandTerrainSignalCapture& Capture,
	const TMap<FGuid, EVoxelLandGraphValueType>& Imported,
	TSharedPtr<const IVoxelLandMaterialSignalSource, ESPMode::ThreadSafe>& Out, FString& Error)
{
	const TSharedRef<FVoxelLandMaterialSignalBindings, ESPMode::ThreadSafe> Bindings =
		MakeShared<FVoxelLandMaterialSignalBindings, ESPMode::ThreadSafe>();
	if (!Bindings->Build(Capture, Imported, Error))
	{
		return false;
	}
	Out = Bindings;
	return true;
}

bool FVoxelLandMaterialSignalBindings::Build(const FVoxelLandTerrainSignalCapture& Capture,
	const TMap<FGuid, EVoxelLandGraphValueType>& Imported, FString& Error)
{
	constexpr int32 MaxImportedSignals = 8;
	if (Imported.Num() > MaxImportedSignals)
	{
		Error = TEXT("At most eight terrain signal imports are supported.");
		return false;
	}
	bUsesFixedLattice = Capture.bUseFixedLattice;
	bUsesThreeDimensions = Capture.bUseThreeDimensions;
	const FVoxelLandGraphSnapshot& Source = Capture.Graph;
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
				*Capture.GraphPath);
			return false;
		}
		if (Import.Value == EVoxelLandGraphValueType::Int32)
		{
			IntegerOutputs.Add(Id);
		}
		FVoxelLandGraphSnapshot Copy = Source;
		if (!FVoxelLandGraphOutputs::Project(Copy, Id, Import.Value != EVoxelLandGraphValueType::Float, Error))
		{
			Error = FString::Printf(TEXT("Terrain signal %s in %s: %s"), *Id.ToString(), *Capture.GraphPath, *Error);
			return false;
		}
		Projected.Add(MoveTemp(Copy));
		OutputIds.Add(Id);
		OutputLabels.Add(Capture.GraphPath + TEXT(" / ") + Id.ToString());
	}

	FVoxelLandGeneratorSettings Settings = Capture.Settings;
	for (int32 Index = 0; Index < Projected.Num(); ++Index)
	{
		FVoxelLandProgramDraft Draft;
		FString CompileError;
		if (!FVoxelLandGraphCompiler::Compile(Projected[Index], Draft, CompileError))
		{
			Error = OutputLabels[Index] + TEXT(": ") + CompileError;
			return false;
		}
		if (const FVoxelLandError Failure = FVoxelLandProgram::Validate(Draft, Settings.Program))
		{
			Error = OutputLabels[Index] + TEXT(": ") + Failure.Message;
			return false;
		}
		TSharedPtr<const FVoxelLandGenerator, ESPMode::ThreadSafe> Generator;
		if (const FVoxelLandError Failure = FVoxelLandGenerator::Create(Settings, Generator))
		{
			Error = OutputLabels[Index] + TEXT(": ") + Failure.Message;
			return false;
		}
		if (bUsesFixedLattice)
		{
			if (const FVoxelLandError Failure = VoxelLandM4FixedLattice::Validate(*Generator))
			{
				Error = OutputLabels[Index] + TEXT(": ") + Failure.Message;
				return false;
			}
		}
		Terrain.Add(OutputIds[Index], MoveTemp(Generator));
	}
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
	const FVector3f Coordinates(static_cast<float>(Position.X), static_cast<float>(Position.Y),
		bUsesThreeDimensions ? static_cast<float>(Position.Z) : 0.f);
	if (IntegerOutputs.Contains(OutputId))
	{
		thread_local TArray<int32> Integers;
		if ((*Generator)->EvaluateIntegerSamples(MakeArrayView(&Coordinates, 1), *Worker, Integers))
		{
			Workers.Remove(Key);
			return false;
		}
		Out = Integers[0];
		return true;
	}
	if ((*Generator)->EvaluateSamplesRaw(MakeArrayView(&Coordinates, 1), *Worker, Values))
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
