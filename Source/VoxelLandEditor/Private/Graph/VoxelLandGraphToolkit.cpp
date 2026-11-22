#include "Graph/VoxelLandGraphToolkit.h"
#include "Data/Assets/VoxelLandM4ControlGraph.h"
#include "../VoxelLandM4EditorBake.h"
#include "Operators/VoxelLandOperator.h"
#include "Graph/VoxelLandEditorFloat.h"
#include "Data/VoxelLandBuiltinCatalog.h"
#include "Data/Structs/VoxelLandCanonicalWriter.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "UObject/PackageReload.h"
#include "UObject/SoftObjectPath.h"
#include "RenderCommandFence.h"
#if WITH_DEV_AUTOMATION_TESTS
#include "Data/Structs/VoxelLandReloadTestBarrier.h"
#endif
#include "Graph/VoxelLandEditorNode.h"
#include "Graph/VoxelLandEditorSchema.h"
#include "Graph/VoxelLandEditorOperator.h"
#include "Graph/VoxelLandPreviewViewport.h"
#include "Graph/VoxelLandM4Inspection.h"
#include "Graph/VoxelLandM4InspectionReport.h"
#include "Graph/VoxelLandM4StageMap.h"
#include "Graph/VoxelLandPreviewScene.h"
#include "Data/Assets/VoxelLandTerrainGraph.h"
#include "Data/Assets/VoxelLandGraphFunction.h"
#include "Data/Assets/VoxelLandWorldSettings.h"
#include "Data/Assets/VoxelLandM4WorldConfig.h"
#include "Data/Structs/VoxelLandM4WorldSnapshot.h"
#include "World/VoxelLandM4Preparation.h"
#include "Data/Assets/VoxelLandMaterialSetup.h"
#include "Data/Enums/VoxelLandBuiltinNode.h"
#include "Data/VoxelLandGraphFunctionCapture.h"
#include "Data/VoxelLandGraphFunctionId.h"
#include "Generation/VoxelLandGenerator.h"
#include "Generation/VoxelLandM4HeightGrid.h"
#include "Generation/VoxelLandM4ControlGrid.h"
#include "Graph/VoxelLandGraphCompiler.h"
#include "Graph/VoxelLandProgramCodec.h"
#include "Generation/VoxelLandMeshing.h"
#include "Rendering/VoxelLandChunkComponent.h"
#include "Rendering/VoxelLandPreviewUploadObservation.h"
#include "HAL/IConsoleManager.h"
#include "AdvancedPreviewScene.h"
#include "AssetRegistry/AssetData.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Materials/MaterialInterface.h"
#include "Materials/Material.h"
#include "Engine/Texture2D.h"
#include "PropertyCustomizationHelpers.h"
#include "Async/Async.h"
#include "EdGraph/EdGraph.h"
#include "EdGraphUtilities.h"
#include "GraphEditAction.h"
#include "Editor.h"
#include "Framework/Commands/GenericCommands.h"
#include "GraphEditor.h"
#include "GraphEditorActions.h"
#include "EdGraphNode_Comment.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Styling/AppStyle.h"
#include "SGraphActionMenu.h"
#include "IDetailsView.h"
#include "PropertyEditorModule.h"
#include "ScopedTransaction.h"
#include "Misc/TransactionObjectEvent.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SSpinBox.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SExpandableArea.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/SBoxPanel.h"
#include "Containers/Ticker.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Images/SImage.h"
#include "HAL/PlatformApplicationMisc.h"
#include "Misc/App.h"

#define LOCTEXT_NAMESPACE "VoxelLandGraphToolkit"

namespace
{
DECLARE_MULTICAST_DELEGATE_OneParam(FM4ControlGraphChanged, UVoxelLandTerrainGraph*);
FM4ControlGraphChanged M4ControlGraphChanged;
void M4ClearNamedPrograms(UVoxelLandM4ControlGraph* Graph)
{
	if (!Graph)
	{
		return;
	}
	const bool bHadPrograms = !Graph->ContinentalProgram.Instructions.IsEmpty()
						   || !Graph->MountainProgram.Instructions.IsEmpty()
						   || !Graph->PeaksProgram.Instructions.IsEmpty();
	Graph->ContinentalProgram = {};
	Graph->MountainProgram = {};
	Graph->PeaksProgram = {};
	if (bHadPrograms)
	{
		Graph->MarkPackageDirty();
		M4ControlGraphChanged.Broadcast(Graph);
	}
}
TAutoConsoleVariable<int32> CVarVoxelLandPreviewGPUUploadTiming(
	TEXT("VoxelLand.Preview.GPUUploadTiming"), 0,
	TEXT("Observe preview buffer upload GPU fences on supported D3D12 editor previews."), ECVF_Default);
#if WITH_DEV_AUTOMATION_TESTS
TAutoConsoleVariable<int32> CVarVoxelLandPreviewGPUUploadHoldForTests(
	TEXT("VoxelLand.Preview.GPUUploadHoldForTests"), 0,
	TEXT("Keep the preview upload observation pending for lifecycle automation."), ECVF_Default);
#endif
const FName GraphTab(TEXT("VoxelLandGraph"));
const FName DetailsTab(TEXT("VoxelLandDetails"));
const FName DiagnosticsTab(TEXT("VoxelLandDiagnostics"));
const FName PreviewTab(TEXT("VoxelLandPreview"));
const FName PaletteTab(TEXT("VoxelLandPalette"));

EVoxelLandGraphValueType PinType(const UEdGraphPin& Pin)
{
	if (Pin.PinType.PinCategory == TEXT("bool"))
	{
		return EVoxelLandGraphValueType::Bool;
	}
	if (Pin.PinType.PinCategory == TEXT("int"))
	{
		return EVoxelLandGraphValueType::Int32;
	}
	return EVoxelLandGraphValueType::Float;
}

FName Category(EVoxelLandGraphValueType Type)
{
	if (Type == EVoxelLandGraphValueType::Bool)
	{
		return TEXT("bool");
	}
	return Type == EVoxelLandGraphValueType::Int32 ? FName(TEXT("int")) : FName(TEXT("float"));
}

bool CopyPin(const UEdGraphPin& Pin, FVoxelLandGraphPinRecord& Record)
{
	Record.Id = Pin.PinId;
	Record.Name = Pin.PinName.ToString();
	Record.bOutput = Pin.Direction == EGPD_Output;
	Record.Type = PinType(Pin);
	if (Record.bOutput)
	{
		return true;
	}
	if (Record.Type == EVoxelLandGraphValueType::Float)
	{
		if (!LexTryParseString(Record.FloatDefault, *Pin.DefaultValue))
		{
			return false;
		}
		return FMath::IsFinite(Record.FloatDefault);
	}
	else if (Record.Type == EVoxelLandGraphValueType::Int32)
	{
		return LexTryParseString(Record.IntDefault, *Pin.DefaultValue);
	}
	else
	{
		if (Pin.DefaultValue.Equals(TEXT("true"), ESearchCase::IgnoreCase) || Pin.DefaultValue == TEXT("1"))
		{
			Record.BoolDefault = true;
		}
		else if (!Pin.DefaultValue.Equals(TEXT("false"), ESearchCase::IgnoreCase) && Pin.DefaultValue != TEXT("0"))
		{
			return false;
		}
	}
	return true;
}

bool SamePinNames(const TArray<FVoxelLandGraphNodeRecord>& Left, const TArray<FVoxelLandGraphNodeRecord>& Right)
{
	TMap<FGuid, FString> Names;
	int32 Count = 0;
	for (const FVoxelLandGraphNodeRecord& Node : Left)
	{
		for (const FVoxelLandGraphPinRecord& Pin : Node.Pins)
		{
			Names.Add(Pin.Id, Pin.Name);
			++Count;
		}
	}
	int32 OtherCount = 0;
	for (const FVoxelLandGraphNodeRecord& Node : Right)
	{
		for (const FVoxelLandGraphPinRecord& Pin : Node.Pins)
		{
			const FString* Name = Names.Find(Pin.Id);
			if (!Name || *Name != Pin.Name) { return false; }
			++OtherCount;
		}
	}
	return Count == OtherCount && Names.Num() == Count;
}

void RestorePin(UVoxelLandEditorNode& Node, const FVoxelLandGraphPinRecord& Record)
{
	UEdGraphPin* Pin = Node.CreatePin(Record.bOutput ? EGPD_Output : EGPD_Input, Category(Record.Type),
		FName(*Record.Name));
	Pin->PinId = Record.Id;
	if (Record.InterfaceId.IsValid())
	{
		Node.FunctionPinIds.Add(Pin->PinId, Record.InterfaceId);
	}
	if (Record.Type == EVoxelLandGraphValueType::Float)
	{
		Pin->DefaultValue = FVoxelLandEditorFloat::Format(Record.FloatDefault);
	}
	else if (Record.Type == EVoxelLandGraphValueType::Int32)
	{
		Pin->DefaultValue = LexToString(Record.IntDefault);
	}
	else
	{
		Pin->DefaultValue = Record.BoolDefault ? TEXT("true") : TEXT("false");
	}
}

bool CaptureNode(const UVoxelLandEditorNode& Node, TArray<FVoxelLandGraphNodeRecord>& Nodes,
	TArray<FVoxelLandGraphLinkRecord>& Links, bool& bValidDefaults, FString& Error,
	FVoxelLandGraphDiagnostic* Diagnostic)
{
	FVoxelLandGraphNodeRecord Record;
	Record.Id = Node.NodeGuid;
	Record.OperatorId = Node.Kind == 0
		                    ? Node.ExternalOperatorId
		                    : VoxelLandBuiltinNodeId(static_cast<EVoxelLandBuiltinNode>(Node.Kind));
	Record.OperatorVersion = Node.OperatorVersion;
	Record.ParameterId = Node.ParameterId;
	Record.SeedSalt = Node.SeedSalt;
	Record.Configuration = Node.Configuration;
	Record.FunctionPath = Node.FunctionPath;
	for (const UEdGraphPin* Pin : Node.Pins)
	{
		if (!Pin)
		{
			continue;
		}
		FVoxelLandGraphPinRecord PinRecord;
		if (!CopyPin(*Pin, PinRecord))
		{
			if (bValidDefaults)
			{
				Error = FString::Printf(TEXT("Invalid default for pin %s."), *Pin->PinName.ToString());
				if (Diagnostic)
				{
					Diagnostic->Message = Error;
					Diagnostic->NodeId = Node.NodeGuid;
					Diagnostic->PinId = Pin->PinId;
				}
			}
			PinRecord.bInvalidDefault = true;
			bValidDefaults = false;
		}
		PinRecord.InterfaceId = Node.FunctionPinIds.FindRef(Pin->PinId);
		Record.Pins.Add(PinRecord);
		if (Pin->Direction == EGPD_Input)
		{
			for (const UEdGraphPin* Linked : Pin->LinkedTo)
			{
				const UVoxelLandEditorNode* Source = Linked
					                                     ? Cast<UVoxelLandEditorNode>(Linked->GetOwningNode())
					                                     : nullptr;
				if (!Source)
				{
					Error = TEXT("Only terrain graph node pins may be connected.");
					if (Diagnostic)
					{
						Diagnostic->Message = Error;
						Diagnostic->NodeId = Node.NodeGuid;
						Diagnostic->PinId = Pin->PinId;
					}
					return false;
				}
				FVoxelLandGraphLinkRecord Link;
				Link.SourceNode = Source->NodeGuid;
				Link.SourcePin = Linked->PinId;
				Link.TargetNode = Node.NodeGuid;
				Link.TargetPin = Pin->PinId;
				Links.Add(Link);
			}
		}
	}
	Nodes.Add(MoveTemp(Record));
	return true;
}

class FVoxelLandPreviewJob
{
public:
	using FResult = TArray<TPair<FIntVector, TSharedPtr<const FVoxelLandMeshData, ESPMode::ThreadSafe>>>;

	static bool Run(const FVoxelLandGraphSnapshot& Snapshot, const FVoxelLandSerializedProgram& Payload,
		int64 Seed, const FVoxelLandZoneProfile& Profile, int32 HorizontalRadius, int32 VerticalRadius,
		const TSharedRef<TAtomic<bool>, ESPMode::ThreadSafe>& Cancel, FResult& Meshes, FString& Error
#if WITH_DEV_AUTOMATION_TESTS
		, const TSharedPtr<FVoxelLandReloadTestBarrier, ESPMode::ThreadSafe>& Barrier
#endif
		)
	{
		TSharedPtr<const FVoxelLandProgram, ESPMode::ThreadSafe> Program;
		if (!FVoxelLandProgramCodec::Load(Snapshot, Payload, Program, Error))
		{
			return false;
		}
		FVoxelLandGeneratorSettings Settings;
		Settings.Program = Program;
		Settings.WorldSeed = uint64(Seed);
		Settings.Profile = Profile;
		TSharedPtr<const FVoxelLandGenerator, ESPMode::ThreadSafe> Generator;
		if (FVoxelLandError Failure = FVoxelLandGenerator::Create(Settings, Generator))
		{
			Error = Failure.Message;
			return false;
		}
		TUniquePtr<FVoxelLandWorkerContext> Worker;
		if (FVoxelLandError Failure = Generator->CreateWorker(Worker))
		{
			Error = Failure.Message;
			return false;
		}
#if WITH_DEV_AUTOMATION_TESTS
		if (Barrier)
		{
			Barrier->WaitForCancellation(Cancel);
			if (Cancel->Load()) { return false; }
		}
#endif
		FVoxelLandMeshingScratch Scratch;
		for (int32 Z = -VerticalRadius; Z < VerticalRadius; ++Z)
		{
			for (int32 Y = -HorizontalRadius; Y <= HorizontalRadius; ++Y)
			{
				for (int32 X = -HorizontalRadius; X <= HorizontalRadius; ++X)
				{
					if (!Generate(*Generator, *Worker, Scratch, FIntVector(X, Y, Z), Profile.VoxelSizeCm, Cancel,
						Meshes, Error))
					{
						return false;
					}
				}
			}
		}
		return !Cancel->Load();
	}

	static bool RunPrepared(const TSharedPtr<const FVoxelLandGenerator, ESPMode::ThreadSafe>& Generator,
		int32 HorizontalRadius, int32 VerticalRadius, const TSharedRef<TAtomic<bool>, ESPMode::ThreadSafe>& Cancel,
		FResult& Meshes, FString& Error)
	{
		return RunPreparedRange(
			Generator, HorizontalRadius, -VerticalRadius, VerticalRadius - 1, Cancel, Meshes, Error);
	}

	static bool RunPreparedRange(const TSharedPtr<const FVoxelLandGenerator, ESPMode::ThreadSafe>& Generator,
		int32 HorizontalRadius, int32 MinZ, int32 MaxZ, const TSharedRef<TAtomic<bool>, ESPMode::ThreadSafe>& Cancel,
		FResult& Meshes, FString& Error, uint64 MaxRetainedMeshBytes = MAX_uint64)
	{
		if (!Generator || Cancel->Load())
		{
			return false;
		}
		TUniquePtr<FVoxelLandWorkerContext> Worker;
		if (const FVoxelLandError Failure = Generator->CreateWorker(Worker))
		{
			Error = Failure.Message;
			return false;
		}
		FVoxelLandMeshingScratch Scratch;
		uint64 RetainedMeshBytes = 0;
		for (int32 Z = MinZ; Z <= MaxZ; ++Z)
		{
			for (int32 Y = -HorizontalRadius; Y <= HorizontalRadius; ++Y)
			{
				for (int32 X = -HorizontalRadius; X <= HorizontalRadius; ++X)
				{
					const int32 PreviousCount = Meshes.Num();
					if (!Generate(*Generator, *Worker, Scratch, FIntVector(X, Y, Z),
							Generator->GetProfile().VoxelSizeCm, Cancel, Meshes, Error))
					{
						return false;
					}
					if (Meshes.Num() > PreviousCount)
					{
						const FVoxelLandMeshData& Mesh = *Meshes.Last().Value;
						RetainedMeshBytes +=
							uint64(Mesh.Vertices.GetAllocatedSize()) + uint64(Mesh.Indices.GetAllocatedSize());
						if (RetainedMeshBytes + uint64(Meshes.GetAllocatedSize()) > MaxRetainedMeshBytes)
						{
							Error = TEXT("Inspection mesh buffers exceed the 64 MiB retention limit.");
							return false;
						}
					}
				}
			}
		}
		return !Cancel->Load();
	}

private:
	static bool Generate(const FVoxelLandGenerator& Generator, FVoxelLandWorkerContext& Worker,
		FVoxelLandMeshingScratch& Scratch, FIntVector Position, float VoxelSize,
		const TSharedRef<TAtomic<bool>, ESPMode::ThreadSafe>& Cancel,
		FResult& Meshes, FString& Error)
	{
		if (Cancel->Load())
		{
			return false;
		}
		FVoxelLandChunkKey Key;
		Key.Coordinate = Position;
		TSharedPtr<const FVoxelLandChunkData, ESPMode::ThreadSafe> Chunk;
		if (FVoxelLandError Failure = Generator.GenerateChunkCancellable(Key, Worker, Chunk, [&Cancel] {
			return Cancel->Load();
		}))
		{
			Error = Failure.Message;
			return false;
		}
		TSharedPtr<FVoxelLandMeshData, ESPMode::ThreadSafe> Mesh = MakeShared<FVoxelLandMeshData,
			ESPMode::ThreadSafe>();
		const EVoxelLandMeshResult Result = VoxelLandMeshing::BuildSurfaceNets(*Chunk, VoxelSize, Scratch, *Mesh,
			[&Cancel] { return Cancel->Load(); });
		if (Result != EVoxelLandMeshResult::Success)
		{
			Error = TEXT("Preview meshing failed or was cancelled.");
			return false;
		}
		if (!Mesh->Indices.IsEmpty())
		{
			Meshes.Emplace(Position, Mesh);
		}
		return true;
	}
};
}

class FVoxelLandNoisePreviewJob
{
public:
	static constexpr int32 Resolution = 128;

	static bool Run(const FVoxelLandGraphSnapshot& Snapshot, const FVoxelLandSerializedProgram& Payload,
		int64 Seed, const FVoxelLandZoneProfile& Profile, float SpacingCm, float SliceZCm,
		const TSharedRef<TAtomic<bool>, ESPMode::ThreadSafe>& Cancel, TArray<uint8>& Pixels,
		float& Minimum, float& Maximum, int32& Clipped, FString& Error)
	{
		TSharedPtr<const FVoxelLandProgram, ESPMode::ThreadSafe> Program;
		if (Cancel->Load() || !FVoxelLandProgramCodec::Load(Snapshot, Payload, Program, Error) || Cancel->Load())
		{
			return false;
		}
		FVoxelLandGeneratorSettings Settings;
		Settings.Program = Program;
		Settings.WorldSeed = uint64(Seed);
		Settings.Profile = Profile;
		TSharedPtr<const FVoxelLandGenerator, ESPMode::ThreadSafe> Generator;
		if (FVoxelLandError Failure = FVoxelLandGenerator::Create(Settings, Generator))
		{
			Error = Failure.Message;
			return false;
		}
		TUniquePtr<FVoxelLandWorkerContext> Worker;
		if (FVoxelLandError Failure = Generator->CreateWorker(Worker))
		{
			Error = Failure.Message;
			return false;
		}
		TArray<uint8> Result;
		Result.SetNumUninitialized(Resolution * Resolution * 4);
		FVector3f Positions[Resolution];
		TArray<float> Values;
		Minimum = TNumericLimits<float>::Max();
		Maximum = TNumericLimits<float>::Lowest();
		Clipped = 0;
		for (int32 Y = 0; Y < Resolution; ++Y)
		{
			if (Cancel->Load()) { return false; }
			for (int32 X = 0; X < Resolution; ++X)
			{
				Positions[X] = FVector3f((float(X) - 63.5f) * SpacingCm, (63.5f - float(Y)) * SpacingCm, SliceZCm);
			}
			if (FVoxelLandError Failure = Generator->EvaluateSamplesRaw(MakeArrayView(Positions, Resolution), *Worker,
				Values))
			{
				Error = Failure.Message;
				return false;
			}
			for (int32 X = 0; X < Resolution; ++X)
			{
				const float Value = Values[X];
				Minimum = FMath::Min(Minimum, Value);
				Maximum = FMath::Max(Maximum, Value);
				if (Value < -1.0f || Value > 1.0f) { ++Clipped; }
				const uint8 Grey = uint8(FMath::RoundToInt((FMath::Clamp(Value, -1.0f, 1.0f) + 1.0f) * 127.5f));
				const int32 Offset = (Y * Resolution + X) * 4;
				Result[Offset] = Grey;
				Result[Offset + 1] = Grey;
				Result[Offset + 2] = Grey;
				Result[Offset + 3] = 255;
			}
		}
		if (Cancel->Load()) { return false; }
		Pixels = MoveTemp(Result);
		return true;
	}
};

class FVoxelLandCompileWork
{
public:
	TAtomic<bool> Done{false};
	uint64 Revision = 0;
	uint64 TargetRevision = 0;
	uint64 SourceHash = 0;
	bool bNodePreview = false;
	bool bNamedM4 = false;
	FGuid NamedOutputIds[3];
	FVoxelLandSerializedProgram NamedPrograms[3];
	FVoxelLandGraphSnapshot Snapshot;
	bool bSuccess = false;
	FString Error;
	FVoxelLandGraphDiagnostic Diagnostic;
	FVoxelLandSerializedProgram Payload;
	double CompileMs = 0;
};

class FVoxelLandPreviewWork
{
public:
	TAtomic<bool> Done{false};
	uint64 PreviewRevision = 0;
	uint64 SourceRevision = 0;
	float VoxelSize = 100.0f;
	int32 ChunkCount = 18;
	bool bSuccess = false;
	bool bNoise2D = false;
	bool bM4 = false;
	bool bM4Inspection = false;
	bool bHasM4HeightRange = false;
	bool bHasM4StageStats = false;
	FVoxelLandM4StageStats M4StageStats;
	int32 M4MinHeightQ8 = 0;
	int32 M4MaxHeightQ8 = 0;
	int32 M4MinChunkZ = 0;
	int32 M4MaxChunkZ = 0;
	int64 M4MinX = 0;
	int64 M4MinY = 0;
	int64 M4MaxX = 0;
	int64 M4MaxY = 0;
	// Members are destroyed in reverse order: release the validation permit
	// only after both bounded input snapshots have been destroyed.
	TSharedPtr<const FVoxelLandM4GridLease, ESPMode::ThreadSafe> M4ValidationLease;
	TSharedPtr<FVoxelLandM4WorldSnapshot, ESPMode::ThreadSafe> M4Snapshot;
	TSharedPtr<FVoxelLandM4WorldSnapshot, ESPMode::ThreadSafe> M4ExecutionSnapshot;
	TWeakObjectPtr<UVoxelLandM4WorldConfig> M4Config;
	TWeakObjectPtr<UVoxelLandTerrainGraph> M4Continental;
	TWeakObjectPtr<UVoxelLandTerrainGraph> M4Mountain;
	TWeakObjectPtr<UVoxelLandTerrainGraph> M4Peaks;
	TArray<uint8> M4StagePixels;
	TArray<uint8> NoisePixels;
	float NoiseMin = 0.0f;
	float NoiseMax = 0.0f;
	int32 NoiseClipped = 0;
	FString Error;
	double GenerationMs = 0;
#if WITH_DEV_AUTOMATION_TESTS
	int64 SeedForTests = 0;
#endif
	FVoxelLandPreviewJob::FResult Meshes;
};

class FVoxelLandPreviewFence
{
public:
	FRenderCommandFence Fence;
	uint64 PreviewRevision = 0;
	uint64 SourceRevision = 0;
	int32 NonemptyChunks = 0;
	int32 ChunkCount = 0;
	double SemanticStart = 0;
	double SetupCompleted = 0;
	double CompileMs = 0;
	double GenerationMs = 0;
	double SetupMs = 0;
	bool bGPURequested = false;
	bool bRenderComplete = false;
	FString ReadyStatus;
	TSharedPtr<FVoxelLandPreviewUploadObservation, ESPMode::ThreadSafe> UploadObservation;
};

FVoxelLandGraphToolkit::~FVoxelLandGraphToolkit()
{
	Stop();
}

void FVoxelLandGraphToolkit::Stop()
{
	if (bStopped)
	{
		return;
	}
	bStopped = true;
	if (bRegisteredForUndo && GEditor)
	{
		GEditor->UnregisterForUndo(this);
		bRegisteredForUndo = false;
	}
	M4ControlGraphChanged.Remove(M4ControlGraphChangedHandle);
	FCoreUObjectDelegates::OnPackageReloaded.Remove(PackageReloadHandle);
	FCoreUObjectDelegates::OnObjectsReplaced.Remove(ObjectsReplacedHandle);
	FCoreUObjectDelegates::OnObjectsReinstanced.Remove(ObjectsReinstancedHandle);
	FCoreUObjectDelegates::OnObjectPropertyChanged.Remove(MaterialSetupChangedHandle);
	FCoreUObjectDelegates::OnObjectTransacted.Remove(M4ObjectTransactedHandle);
	MaterialSetupChangedHandle.Reset();
	M4ObjectTransactedHandle.Reset();
	FEditorDelegates::PreBeginPIE.Remove(BeginPIEHandle);
	FEditorDelegates::EndPIE.Remove(EndPIEHandle);
	BeginPIEHandle.Reset();
	EndPIEHandle.Reset();
	if (Graph && GraphChangedHandle.IsValid())
	{
		Graph->RemoveOnGraphChangedHandler(GraphChangedHandle);
		GraphChangedHandle.Reset();
	}
	if (Details)
	{
		Details->OnFinishedChangingProperties().RemoveAll(this);
	}
	if (PreviewDetails)
	{
		PreviewDetails->OnFinishedChangingProperties().RemoveAll(this);
	}
	CancelPreview();
	FTSTicker::RemoveTicker(PollHandle);
	for (const TFuture<void>& Task : WorkerTasks)
	{
		Task.Wait();
	}
	WorkerTasks.Reset();
	CompileWork.Reset();
	PreviewWork.Reset();
	M4StageBrush.SetResourceObject(nullptr);
	M4StageTexture = nullptr;
	M4ControlsBrush.SetResourceObject(nullptr);
	M4ControlsTexture = nullptr;
	NoiseBrush.SetResourceObject(nullptr);
	NoiseTexture = nullptr;
	ClearPreview();
}

void FVoxelLandGraphToolkit::AddReferencedObjects(FReferenceCollector& Collector)
{
	Collector.AddReferencedObject(Asset);
	Collector.AddReferencedObject(Graph);
	Collector.AddReferencedObjects(PreviewComponents);
	Collector.AddReferencedObject(PreviewMaterial);
	Collector.AddReferencedObject(NoiseTexture);
	Collector.AddReferencedObject(M4StageTexture);
	Collector.AddReferencedObject(M4ControlsTexture);
	Collector.AddReferencedObject(PreviewSettings);
	Collector.AddReferencedObject(M4PreviewConfig);
}

FText FVoxelLandGraphToolkit::GetBaseToolkitName() const
{
	return LOCTEXT("Title", "VoxelLand Terrain Graph");
}

bool FVoxelLandGraphToolkit::StoreCompiledProgram(UVoxelLandTerrainGraph& Graph, FVoxelLandSerializedProgram&& Payload)
{
	if (FVoxelLandSerializedProgram::StaticStruct()->CompareScriptStruct(&Graph.CompiledProgram, &Payload, 0))
	{
		return false;
	}
	Graph.Modify();
	Graph.CompiledProgram = MoveTemp(Payload);
	Graph.MarkPackageDirty();
	M4ControlGraphChanged.Broadcast(&Graph);
	return true;
}

void FVoxelLandGraphToolkit::Open(UVoxelLandTerrainGraph* InAsset, EToolkitMode::Type Mode,
	const TSharedPtr<IToolkitHost>& Host)
{
	Asset = InAsset;
	M4ControlGraphChangedHandle =
		M4ControlGraphChanged.AddRaw(this, &FVoxelLandGraphToolkit::M4ControlGraphSourceChanged);
	MaterialSetupChangedHandle = FCoreUObjectDelegates::OnObjectPropertyChanged.AddRaw(this,
		&FVoxelLandGraphToolkit::MaterialSetupPropertyChanged);
	M4ObjectTransactedHandle =
		FCoreUObjectDelegates::OnObjectTransacted.AddRaw(this, &FVoxelLandGraphToolkit::M4ObjectTransacted);
	PackageReloadHandle = FCoreUObjectDelegates::OnPackageReloaded.AddRaw(this,
		&FVoxelLandGraphToolkit::PackageReloaded);
	ObjectsReplacedHandle = FCoreUObjectDelegates::OnObjectsReplaced.AddRaw(this,
		&FVoxelLandGraphToolkit::ObjectsReplaced);
	ObjectsReinstancedHandle = FCoreUObjectDelegates::OnObjectsReinstanced.AddRaw(this,
		&FVoxelLandGraphToolkit::ObjectsReinstanced);
	if (GEditor)
	{
		GEditor->RegisterForUndo(this);
		bRegisteredForUndo = true;
	}
	PreviewScene = MakeShared<FVoxelLandPreviewScene>();
	PreviewViewport = SNew(SVoxelLandPreviewViewport).Scene(PreviewScene);
	PreviewSettings = NewObject<UVoxelLandWorldSettings>(GetTransientPackage(), NAME_None,
		RF_Transient | RF_Transactional);
	PreviewSettings->Graph = Asset;
	SynchronizePreviewSettings(false);
	ApplyPreviewMaterial(nullptr);
	CreateMirror();
	FVoxelLandGraphSnapshot InitialSource;
	InitialSource.SourceVersion = Asset->SourceVersion;
	InitialSource.Parameters = Asset->Parameters;
	InitialSource.Nodes = Asset->Nodes;
	InitialSource.Links = Asset->Links;
	LastSourceHash = FVoxelLandProgramCodec::SourceFingerprint(InitialSource);
	FString InitialError;
	CaptureMirror(InitialError);
	BindCommands();
	FPropertyEditorModule& PropertyModule = FModuleManager::LoadModuleChecked<FPropertyEditorModule>(
		TEXT("PropertyEditor"));
	Details = PropertyModule.CreateDetailView(FDetailsViewArgs());
	PreviewDetails = PropertyModule.CreateDetailView(FDetailsViewArgs());
	PreviewDetails->SetObject(PreviewSettings);
	PreviewDetails->OnFinishedChangingProperties().AddRaw(this, &FVoxelLandGraphToolkit::PreviewPropertiesChanged);
	Details->SetObject(Asset);
	Details->OnFinishedChangingProperties().AddRaw(this, &FVoxelLandGraphToolkit::PropertiesChanged);
	SGraphEditor::FGraphEditorEvents Events;
	Events.OnSelectionChanged = SGraphEditor::FOnSelectionChanged::CreateSP(this,
		&FVoxelLandGraphToolkit::SelectionChanged);
	GraphWidget =
		SNew(SGraphEditor).AdditionalCommands(Commands).GraphToEdit(Graph).GraphEvents(Events).IsEditable(true);
	GraphChangedHandle = Graph->AddOnGraphChangedHandler(
		FOnGraphChanged::FDelegate::CreateRaw(this, &FVoxelLandGraphToolkit::GraphChanged));
	PollHandle = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateSP(this, &FVoxelLandGraphToolkit::PollJobs), 0.05f);
	BeginPIEHandle = FEditorDelegates::PreBeginPIE.AddRaw(this, &FVoxelLandGraphToolkit::BeginPIE);
	EndPIEHandle = FEditorDelegates::EndPIE.AddRaw(this, &FVoxelLandGraphToolkit::EndPIE);
	const TSharedRef<FTabManager::FLayout> Layout = FTabManager::NewLayout(TEXT("VoxelLandGraphEditor_v1"))
		->AddArea(FTabManager::NewPrimaryArea()->SetOrientation(Orient_Vertical)
		                                       ->Split(FTabManager::NewSplitter()->SetOrientation(Orient_Horizontal)
			                                       ->Split(FTabManager::NewStack()->SetSizeCoefficient(0.50f)->AddTab(
				                                       GraphTab, ETabState::OpenedTab))
			                                       ->Split(FTabManager::NewStack()->SetSizeCoefficient(0.30f)->AddTab(
				                                       PreviewTab, ETabState::OpenedTab))
			                                       ->Split(FTabManager::NewStack()->SetSizeCoefficient(0.20f)->AddTab(
				                                       DetailsTab, ETabState::OpenedTab)->AddTab(PaletteTab,
				                                       ETabState::OpenedTab)))
		                                       ->Split(FTabManager::NewStack()->SetSizeCoefficient(0.2f)->AddTab(
			                                       DiagnosticsTab, ETabState::OpenedTab)));
	InitAssetEditor(Mode, Host, TEXT("VoxelLandGraphEditor"), Layout, true, true, Asset);
	bHasEditorWindow = true;
	ExtendGraphToolbar();
	RegenerateMenusAndToolbars();
	RequestCompile();
}
#if WITH_DEV_AUTOMATION_TESTS
void FVoxelLandGraphToolkit::SetMaterialPickerForTests(UMaterialInterface* Material)
{
	MaterialChanged(Material ? FAssetData(Material) : FAssetData());
}

void FVoxelLandGraphToolkit::SetSettingsPickerForTests(UVoxelLandWorldSettings* Settings)
{
	SettingsChanged(Settings ? FAssetData(Settings) : FAssetData());
}

void FVoxelLandGraphToolkit::InitializePreviewForTests()
{
	PreviewScene = MakeShared<FVoxelLandPreviewScene>();
	PreviewViewport = SNew(SVoxelLandPreviewViewport).Scene(PreviewScene);
	ApplyPreviewMaterial(nullptr);
	PollHandle = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateSP(this, &FVoxelLandGraphToolkit::PollJobs), 0.05f);
	BeginPIEHandle = FEditorDelegates::PreBeginPIE.AddRaw(this, &FVoxelLandGraphToolkit::BeginPIE);
	EndPIEHandle = FEditorDelegates::EndPIE.AddRaw(this, &FVoxelLandGraphToolkit::EndPIE);
}

FBox FVoxelLandGraphToolkit::GetPreviewBoundsForTests() const
{
	return PreviewViewport ? PreviewViewport->GetTerrainBounds() : FBox(ForceInit);
}

FBox FVoxelLandGraphToolkit::GetComponentBoundsForTests() const
{
	FBox Bounds(ForceInit);
	for (const UVoxelLandChunkComponent* Component : PreviewComponents)
	{
		if (Component)
		{
			Bounds += Component->Bounds.GetBox();
		}
	}
	return Bounds;
}

bool FVoxelLandGraphToolkit::FramePreviewForTests()
{
	return !bStopped && PreviewViewport && PreviewViewport->FrameTerrain();
}

void FVoxelLandGraphToolkit::CreatePanelForTests()
{
	GraphWidget = SNew(SGraphEditor).GraphToEdit(Graph).IsEditable(true);
}

void FVoxelLandGraphToolkit::CopyPasteNodesForTests(const TArray<UEdGraphNode*>& Nodes)
{
	GraphWidget->ClearSelectionSet();
	for (UEdGraphNode* Node : Nodes)
	{
		GraphWidget->SetNodeSelection(Node, true);
	}
	CopySelected();
	PasteNodes();
}

void FVoxelLandGraphToolkit::InitializeForTests(UVoxelLandTerrainGraph* InAsset)
{
	Asset = InAsset;
	M4ControlGraphChangedHandle =
		M4ControlGraphChanged.AddRaw(this, &FVoxelLandGraphToolkit::M4ControlGraphSourceChanged);
	MaterialSetupChangedHandle = FCoreUObjectDelegates::OnObjectPropertyChanged.AddRaw(this,
		&FVoxelLandGraphToolkit::MaterialSetupPropertyChanged);
	M4ObjectTransactedHandle =
		FCoreUObjectDelegates::OnObjectTransacted.AddRaw(this, &FVoxelLandGraphToolkit::M4ObjectTransacted);
	PackageReloadHandle = FCoreUObjectDelegates::OnPackageReloaded.AddRaw(this,
		&FVoxelLandGraphToolkit::PackageReloaded);
	ObjectsReplacedHandle = FCoreUObjectDelegates::OnObjectsReplaced.AddRaw(this,
		&FVoxelLandGraphToolkit::ObjectsReplaced);
	ObjectsReinstancedHandle = FCoreUObjectDelegates::OnObjectsReinstanced.AddRaw(this,
		&FVoxelLandGraphToolkit::ObjectsReinstanced);
	PreviewSettings = NewObject<UVoxelLandWorldSettings>(GetTransientPackage(), NAME_None,
		RF_Transient | RF_Transactional);
	PreviewSettings->Graph = Asset;
	SynchronizePreviewSettings(false);
	GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->NotifyAssetOpened(Asset, this);
	CreateMirror();
	FString Error;
	CaptureMirror(Error);
	GraphChangedHandle = Graph->AddOnGraphChangedHandler(
		FOnGraphChanged::FDelegate::CreateRaw(this, &FVoxelLandGraphToolkit::GraphChanged));
	GEditor->RegisterForUndo(this);
	bRegisteredForUndo = true;
}
#endif
void FVoxelLandGraphToolkit::RegisterTabSpawners(const TSharedRef<FTabManager>& Manager)
{
	FAssetEditorToolkit::RegisterTabSpawners(Manager);
	Manager->RegisterTabSpawner(GraphTab, FOnSpawnTab::CreateSP(this, &FVoxelLandGraphToolkit::SpawnGraph)).
	         SetDisplayName(LOCTEXT("Graph", "Graph"));
	Manager->RegisterTabSpawner(DetailsTab, FOnSpawnTab::CreateSP(this, &FVoxelLandGraphToolkit::SpawnDetails)).
	         SetDisplayName(LOCTEXT("Details", "Details / Parameters"));
	Manager->RegisterTabSpawner(DiagnosticsTab, FOnSpawnTab::CreateSP(this, &FVoxelLandGraphToolkit::SpawnDiagnostics)).
	         SetDisplayName(LOCTEXT("Diagnostics", "Compiler Results"));
	Manager->RegisterTabSpawner(PreviewTab, FOnSpawnTab::CreateSP(this, &FVoxelLandGraphToolkit::SpawnPreview)).
	         SetDisplayName(LOCTEXT("Preview", "Preview"));
	Manager->RegisterTabSpawner(PaletteTab, FOnSpawnTab::CreateSP(this, &FVoxelLandGraphToolkit::SpawnPalette)).
	         SetDisplayName(LOCTEXT("Palette", "Node Palette"));
}

void FVoxelLandGraphToolkit::UnregisterTabSpawners(const TSharedRef<FTabManager>& Manager)
{
	Manager->UnregisterTabSpawner(GraphTab);
	Manager->UnregisterTabSpawner(DetailsTab);
	Manager->UnregisterTabSpawner(DiagnosticsTab);
	Manager->UnregisterTabSpawner(PreviewTab);
	Manager->UnregisterTabSpawner(PaletteTab);
	FAssetEditorToolkit::UnregisterTabSpawners(Manager);
}

TSharedRef<SDockTab> FVoxelLandGraphToolkit::SpawnGraph(const FSpawnTabArgs&)
{
	return SNew(SDockTab)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(SButton).Text(LOCTEXT("FrameSelection", "Frame selection")).OnClicked_Lambda([this] {
					FrameSelection();
					return FReply::Handled();
				})
			]
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(SComboButton)
				.Visibility_Lambda([this] {
					return Asset && Asset->IsA<UVoxelLandGraphFunction>()
						       ? EVisibility::Visible
						       : EVisibility::Collapsed;
				})
				.ButtonContent()
				[
					SNew(STextBlock).Text(LOCTEXT("FunctionOutput", "Set selected function output"))
				]
				.OnGetMenuContent_Raw(this, &FVoxelLandGraphToolkit::BuildFunctionOutputMenu)
			]
			+ SVerticalBox::Slot().FillHeight(1)
			[
				GraphWidget.ToSharedRef()
			]
		];
}

TSharedRef<SDockTab> FVoxelLandGraphToolkit::SpawnPalette(const FSpawnTabArgs&)
{
	PresentationRevision = FVoxelLandEditorOperatorRegistry::GetRevision();
	SAssignNew(PaletteWidget, SGraphActionMenu)
	.GraphObj(Graph)
	.ShowFilterTextBox(true)
	.OnCollectAllActions_Raw(this, &FVoxelLandGraphToolkit::CollectPalette)
	.OnActionDoubleClicked_Raw(this, &FVoxelLandGraphToolkit::ActivatePalette);
	return SNew(SDockTab)
		[
			PaletteWidget.ToSharedRef()
		];
}

void FVoxelLandGraphToolkit::CollectPalette(FGraphActionListBuilderBase& OutActions)
{
	FGraphContextMenuBuilder Context(Graph);
	Graph->GetSchema()->GetGraphContextActions(Context);
	OutActions.Append(Context);
}

void FVoxelLandGraphToolkit::ActivatePalette(const TArray<TSharedPtr<FEdGraphSchemaAction>>& Actions)
{
	if (!GraphWidget || Actions.IsEmpty() || !Actions[0])
	{
		return;
	}
	Actions[0]->PerformAction(Graph, static_cast<UEdGraphPin*>(nullptr), GraphWidget->GetPasteLocation2f());
}

TSharedRef<SDockTab> FVoxelLandGraphToolkit::SpawnDetails(const FSpawnTabArgs&)
{
	return SNew(SDockTab)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(STextBlock)
				.Text(LOCTEXT("ParameterHint",
					"Select the background for graph parameters, or a parameter node to choose its ID."))
				.AutoWrapText(true)
			]
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().FillWidth(1)
				[
					SNew(SButton).Text(LOCTEXT("AddFloat", "+ Float")).OnClicked_Lambda([this] {
						return AddParameter(EVoxelLandGraphValueType::Float);
					})
				]
				+ SHorizontalBox::Slot().FillWidth(1)
				[
					SNew(SButton).Text(LOCTEXT("AddInt", "+ Int")).OnClicked_Lambda([this] {
						return AddParameter(EVoxelLandGraphValueType::Int32);
					})
				]
				+ SHorizontalBox::Slot().FillWidth(1)
				[
					SNew(SButton).Text(LOCTEXT("AddBool", "+ Bool")).OnClicked_Lambda([this] {
						return AddParameter(EVoxelLandGraphValueType::Bool);
					})
				]
			]
			+ SVerticalBox::Slot().FillHeight(1)
			[
				Details.ToSharedRef()
			]
		];
}

FReply FVoxelLandGraphToolkit::AddParameter(EVoxelLandGraphValueType Type)
{
	const FScopedTransaction Transaction(LOCTEXT("AddParameter", "Add terrain graph parameter"));
	Asset->Modify();
	FVoxelLandGraphParameter Parameter;
	Parameter.Id = FGuid::NewGuid();
	Parameter.Type = Type;
	int32 Suffix = Asset->Parameters.Num() + 1;
	do
	{
		Parameter.Name = FString::Printf(TEXT("Parameter%d"), Suffix++);
	}
	while (Asset->Parameters.ContainsByPredicate([&Parameter](const FVoxelLandGraphParameter& Existing) {
		return Existing.Name.Equals(Parameter.Name, ESearchCase::IgnoreCase);
	}));
	Asset->Parameters.Add(MoveTemp(Parameter));
	if (Asset->IsA<UVoxelLandGraphFunction>())
	{
		FVoxelLandEditorOperatorRegistry::NotifyFunctionChanged();
	}
	Details->SetObject(Asset, true);
	GraphChanged(FEdGraphEditAction());
	return FReply::Handled();
}

TSharedRef<SDockTab> FVoxelLandGraphToolkit::SpawnDiagnostics(const FSpawnTabArgs&)
{
	return SNew(
		SDockTab)[SNew(SScrollBox)
				  + SScrollBox::Slot()
					  [SNew(SVerticalBox)
						  + SVerticalBox::Slot().AutoHeight().Padding(4)
							  [SNew(STextBlock).AutoWrapText(true).Text_Raw(this, &FVoxelLandGraphToolkit::StatusText)]
						  + SVerticalBox::Slot().AutoHeight().Padding(4)[SNew(STextBlock)
								  .AutoWrapText(true)
								  .Text_Raw(this, &FVoxelLandGraphToolkit::M4StageInspectorText)]
						  + SVerticalBox::Slot().AutoHeight().Padding(
							  4)[SNew(SBox).WidthOverride(256).HeightOverride(256).Visibility_Lambda([this] {
								return M4StageTexture ? EVisibility::Visible : EVisibility::Collapsed;
							})[SNew(SImage).Image_Raw(this, &FVoxelLandGraphToolkit::GetM4StageBrush)]]
						  + SVerticalBox::Slot().AutoHeight().Padding(
							  4)[SNew(SBox).WidthOverride(384).HeightOverride(128).Visibility_Lambda([this] {
								return M4ControlsTexture ? EVisibility::Visible : EVisibility::Collapsed;
							})[SNew(SImage).Image_Raw(this, &FVoxelLandGraphToolkit::GetM4ControlsBrush)]]]];
}

FText FVoxelLandGraphToolkit::StatusText() const
{
	const FString Mode =
		bM4Preview ? FString(bM4Inspection ? TEXT("M4 2 km inspection. ") : TEXT("M4 terrain preview. "))
		: bNoisePreview
			? FString::Printf(TEXT("2D noise %s - %s. "),
				  bNoiseStandalone ? TEXT("standalone coordinates") : TEXT("graph-wired slice"), *PreviewTargetLabel)
		: PreviewNodeId.IsValid() ? FString::Printf(TEXT("Node preview - %s. "), *PreviewTargetLabel)
								  : TEXT("Root preview. ");
	return FText::FromString(
		Mode + Status + (MaterialDiagnostic.IsEmpty() ? FString() : TEXT(" ") + MaterialDiagnostic));
}

FText FVoxelLandGraphToolkit::M4StageInspectorText() const
{
	return FText::FromString(
		M4StageDiagnostics.IsEmpty()
			? TEXT("M4 global-stage inspector: generate a finite M4 preview to inspect its completed height grid.")
			: M4StageDiagnostics);
}

bool FVoxelLandGraphToolkit::ResolveDiagnosticTarget(UEdGraphNode*& OutNode, UEdGraphPin*& OutPin) const
{
	OutNode = nullptr;
	OutPin = nullptr;
	if (!Graph || !ErrorDiagnostic.NodeId.IsValid()) { return false; }
	for (UEdGraphNode* Node : Graph->Nodes)
	{
		if (Node && Node->NodeGuid == ErrorDiagnostic.NodeId)
		{
			OutNode = Node;
			if (ErrorDiagnostic.PinId.IsValid())
			{
				for (UEdGraphPin* Pin : Node->Pins)
				{
					if (Pin && Pin->PinId == ErrorDiagnostic.PinId)
					{
						OutPin = Pin;
						break;
					}
				}
			}
			return true;
		}
	}
	return false;
}

FReply FVoxelLandGraphToolkit::FocusError()
{
	UEdGraphNode* Node = nullptr;
	UEdGraphPin* Pin = nullptr;
	if (GraphWidget && ResolveDiagnosticTarget(Node, Pin))
	{
		if (Pin) { GraphWidget->JumpToPin(Pin); }
		else { GraphWidget->JumpToNode(Node); }
	}
	return FReply::Handled();
}

TSharedRef<SDockTab> FVoxelLandGraphToolkit::SpawnPreview(const FSpawnTabArgs&)
{
	return SNew(SDockTab)
		[SNew(SVerticalBox)
			+ SVerticalBox::Slot()
				.AutoHeight()[SNew(STextBlock).Text_Raw(this, &FVoxelLandGraphToolkit::StatusText).AutoWrapText(true)]
			+ SVerticalBox::Slot().AutoHeight()
				[SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().FillWidth(1)[SNew(SObjectPropertyEntryBox)
							.AllowedClass(UVoxelLandM4WorldConfig::StaticClass())
							.ObjectPath_Raw(this, &FVoxelLandGraphToolkit::M4PreviewConfigPath)
							.OnObjectChanged_Raw(this, &FVoxelLandGraphToolkit::M4PreviewConfigChanged)]
					+ SHorizontalBox::Slot().AutoWidth()
						[SNew(SButton).Text(LOCTEXT("M4PreviewGenerate", "Preview M4 world")).OnClicked_Lambda([this] {
							bM4Preview = true;
							bM4Inspection = false;
							bNoisePreview = false;
							StartM4Preview();
							return FReply::Handled();
						})]
					+ SHorizontalBox::Slot().AutoWidth()
						[SNew(SButton).Text(LOCTEXT("M4Inspect2km", "Inspect from 2 km")).OnClicked_Lambda([this] {
							bM4Preview = true;
							bM4Inspection = true;
							bNoisePreview = false;
							StartM4Preview();
							return FReply::Handled();
						})]
					+ SHorizontalBox::Slot().AutoWidth()
						[SNew(SButton).Text(LOCTEXT("M4CopyInspection", "Copy inspection details")).OnClicked_Lambda([this] {
							if (!M4InspectionDetails.IsEmpty())
							{
								FString Details = M4InspectionDetails;
								FVector Location;
								FRotator Rotation;
								float Fov = 0.f;
								FIntPoint Resolution;
								if (PreviewViewport
									&& PreviewViewport->GetCameraDetails(Location, Rotation, Fov, Resolution))
								{
									Details += FString::Printf(
										TEXT(
											"\nView at time of copy: %s; rotation %s; horizontal FOV %.2f degrees; viewport %d x %d."),
										*Location.ToString(), *Rotation.ToString(), Fov, Resolution.X, Resolution.Y);
								}
								FPlatformApplicationMisc::ClipboardCopy(*Details);
							}
							return FReply::Handled();
						})]]
			+ SVerticalBox::Slot().AutoHeight()
				[SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth()[SNew(SComboButton)
							.ButtonContent()[SNew(STextBlock).Text(LOCTEXT("PreviewOutput", "Preview selected output"))]
							.OnGetMenuContent_Raw(this, &FVoxelLandGraphToolkit::BuildPreviewTargetMenu)]
					+ SHorizontalBox::Slot().AutoWidth()[SNew(SButton)
							.Text(LOCTEXT("PreviewRoot", "Preview graph output"))
							.OnClicked_Raw(this, &FVoxelLandGraphToolkit::ReturnToRootPreview)]]
			+ SVerticalBox::Slot().AutoHeight()
				[SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth()[SNew(SButton)
							.Text(LOCTEXT("NoiseWired", "2D noise: graph-wired"))
							.ToolTipText(LOCTEXT(
								"NoiseWiredTip", "Sample the selected noise node with its existing graph connections."))
							.OnClicked_Lambda([this] { return SelectNoisePreview(false); })]
					+ SHorizontalBox::Slot().AutoWidth()[SNew(SButton)
							.Text(LOCTEXT("NoiseStandalone", "2D noise: standalone"))
							.ToolTipText(LOCTEXT("NoiseStandaloneTip",
								"Temporarily substitute XY and Z positions for the selected noise node's coordinate inputs."))
							.OnClicked_Lambda([this] { return SelectNoisePreview(true); })]]
			+ SVerticalBox::Slot()
				.AutoHeight()[SNew(SHorizontalBox).Visibility_Lambda([this] {
					return bNoisePreview ? EVisibility::Visible : EVisibility::Collapsed;
				})
							  + SHorizontalBox::Slot().AutoWidth().VAlign(
								  VAlign_Center)[SNew(STextBlock).Text(LOCTEXT("NoiseSpacing", "Spacing (cm/pixel)"))]
							  + SHorizontalBox::Slot().FillWidth(1)[SNew(SSpinBox<float>)
									  .MinValue(0.01f)
									  .MaxValue(100000.0f)
									  .Value_Lambda([this] { return NoiseSpacingCm; })
									  .OnValueCommitted_Raw(this, &FVoxelLandGraphToolkit::SetNoiseSpacing)]
							  + SHorizontalBox::Slot().AutoWidth().VAlign(
								  VAlign_Center)[SNew(STextBlock).Text(LOCTEXT("NoiseSlice", "World Z (cm)"))]
							  + SHorizontalBox::Slot().FillWidth(1)[SNew(SSpinBox<float>)
									  .MinValue(-10000000.0f)
									  .MaxValue(10000000.0f)
									  .Value_Lambda([this] { return NoiseSliceZCm; })
									  .OnValueCommitted_Raw(this, &FVoxelLandGraphToolkit::SetNoiseSlice)]]
			+ SVerticalBox::Slot().AutoHeight()[SNew(STextBlock)
					.Visibility_Lambda([this] { return bNoisePreview ? EVisibility::Visible : EVisibility::Collapsed; })
					.Text(LOCTEXT("NoiseLegend",
						"128 x 128; X right, Y up. Black = -1, grey = 0, white = +1; values outside [-1, 1] clip. Graph-wired mode samples the graph at world Z; Z only affects connected inputs."))
					.AutoWrapText(true)]
			+ SVerticalBox::Slot().AutoHeight()
				[SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth().VAlign(
						VAlign_Center)[SNew(STextBlock).Text(LOCTEXT("Seed", "Seed"))]
					+ SHorizontalBox::Slot().FillWidth(1)[SNew(SEditableTextBox)
							.Text_Lambda([this] { return FText::AsCultureInvariant(LexToString(PreviewSeed)); })
							.OnTextCommitted_Raw(this, &FVoxelLandGraphToolkit::SeedCommitted)]
					+ SHorizontalBox::Slot().AutoWidth()[SNew(SButton)
							.Text(LOCTEXT("FrameTerrain", "Frame terrain (F)"))
							.OnClicked_Raw(this, &FVoxelLandGraphToolkit::FramePreviewTerrain)]
					+ SHorizontalBox::Slot().AutoWidth()[SNew(SButton)
							.Text(LOCTEXT("ResetCamera", "Reset camera"))
							.OnClicked_Raw(this, &FVoxelLandGraphToolkit::ResetPreviewCamera)]]
			+ SVerticalBox::Slot().AutoHeight()[SNew(SObjectPropertyEntryBox)
					.AllowedClass(UMaterialInterface::StaticClass())
					.ObjectPath_Raw(this, &FVoxelLandGraphToolkit::MaterialPickerPath)
					.OnObjectChanged_Raw(this, &FVoxelLandGraphToolkit::MaterialChanged)]
			+ SVerticalBox::Slot().AutoHeight()[SNew(SObjectPropertyEntryBox)
					.AllowedClass(UVoxelLandWorldSettings::StaticClass())
					.ObjectPath_Raw(this, &FVoxelLandGraphToolkit::SettingsPickerPath)
					.OnObjectChanged_Raw(this, &FVoxelLandGraphToolkit::SettingsChanged)]
			+ SVerticalBox::Slot()
				.AutoHeight()[SNew(SHorizontalBox)
							  + SHorizontalBox::Slot().AutoWidth().VAlign(
								  VAlign_Center)[SNew(STextBlock).Text(LOCTEXT("Radius", "Preview radius (XY / Z)"))]
							  + SHorizontalBox::Slot().FillWidth(1)[SNew(SSpinBox<int32>)
									  .MinValue(1)
									  .MaxValue(2)
									  .Value_Lambda([this] { return PreviewHorizontalRadius; })
									  .OnValueCommitted_Lambda(
										  [this](int32 Value, ETextCommit::Type) { CommitPreviewRadius(Value, true); })]
							  + SHorizontalBox::Slot().FillWidth(1)[SNew(SSpinBox<int32>)
									  .MinValue(1)
									  .MaxValue(2)
									  .Value_Lambda([this] { return PreviewVerticalRadius; })
									  .OnValueCommitted_Lambda([this](int32 Value, ETextCommit::Type) {
										  CommitPreviewRadius(Value, false);
									  })]]
			+ SVerticalBox::Slot().AutoHeight()[SNew(SExpandableArea)
					.InitiallyCollapsed(true)
					.HeaderContent()[SNew(STextBlock)
							.Text(LOCTEXT("PreviewSettings", "Transient preview settings and overrides"))]
					.BodyContent()[SNew(SBox).MaxDesiredHeight(250)[PreviewDetails.ToSharedRef()]]]
			+ SVerticalBox::Slot().FillHeight(1)[SNew(SBox).Visibility_Lambda([this] {
				  return bNoisePreview ? EVisibility::Collapsed : EVisibility::Visible;
			  })[PreviewViewport.ToSharedRef()]]
			+ SVerticalBox::Slot().FillHeight(1)[SNew(SBox)
					.HAlign(HAlign_Center)
					.VAlign(VAlign_Center)
					.Visibility_Lambda([this] { return bNoisePreview ? EVisibility::Visible : EVisibility::Collapsed; })
						[SNew(SBox).WidthOverride(384.0f).HeightOverride(
							384.0f)[SNew(SImage).Image_Raw(this, &FVoxelLandGraphToolkit::GetNoiseBrush)]]]];
}

TSharedRef<SWidget> FVoxelLandGraphToolkit::BuildFunctionOutputMenu()
{
	FMenuBuilder Menu(true, nullptr);
	bool bHasOutput = false;
	if (GraphWidget && Asset && Asset->IsA<UVoxelLandGraphFunction>())
	{
		for (UObject* Object : GraphWidget->GetSelectedNodes())
		{
			const UVoxelLandEditorNode* Node = Cast<UVoxelLandEditorNode>(Object);
			if (!Node) { continue; }
			for (const UEdGraphPin* Pin : Node->Pins)
			{
				if (!Pin || Pin->Direction != EGPD_Output) { continue; }
				bHasOutput = true;
				const FString Label = FString::Printf(TEXT("%s - %s (%s)"),
					*Node->GetNodeTitle(ENodeTitleType::ListView).ToString(), *Node->GetPinDisplayName(Pin).ToString(),
					*Pin->PinType.PinCategory.ToString());
				Menu.AddMenuEntry(FText::FromString(Label),
					LOCTEXT("FunctionOutputTip", "Use this output as the function result."),
					FSlateIcon(), FUIAction(FExecuteAction::CreateSP(this, &FVoxelLandGraphToolkit::SetFunctionOutput,
						Node->NodeGuid, Pin->PinId, PinType(*Pin))));
			}
		}
	}
	if (!bHasOutput)
	{
		Menu.AddMenuEntry(LOCTEXT("NoFunctionOutput", "Select a node with an output"), FText::GetEmpty(), FSlateIcon(),
			FUIAction(FExecuteAction(), FCanExecuteAction::CreateLambda([] { return false; })));
	}
	return Menu.MakeWidget();
}

void FVoxelLandGraphToolkit::SetFunctionOutput(FGuid NodeId, FGuid PinId, EVoxelLandGraphValueType Type)
{
	UVoxelLandGraphFunction* Function = Cast<UVoxelLandGraphFunction>(Asset);
	if (!Function || bStopped || bPIE || bReloadPaused) { return; }
	const UVoxelLandEditorNode* Node = nullptr;
	if (Graph)
	{
		for (const UEdGraphNode* Candidate : Graph->Nodes)
		{
			if (Candidate && Candidate->NodeGuid == NodeId)
			{
				Node = Cast<UVoxelLandEditorNode>(Candidate);
				break;
			}
		}
	}
	if (!Node) { return; }
	UEdGraphPin* const* FoundPin = Node->Pins.FindByPredicate([PinId](const UEdGraphPin* Pin) {
		return Pin && Pin->PinId == PinId && Pin->Direction == EGPD_Output;
	});
	if (!FoundPin || PinType(**FoundPin) != Type) { return; }
	const auto Provider = FVoxelLandOperatorRegistry::Resolve(Node->ExternalOperatorId, Node->OperatorVersion);
	if (Node->Kind == int32(EVoxelLandBuiltinNode::SmoothDensity) || (
		    Node->Kind == 0 && Provider && Provider->Stage() == EVoxelLandOperatorStage::OneCellFilter))
	{
		Status = TEXT("Error: a density filter cannot be a function output.");
		return;
	}
	const FScopedTransaction Transaction(LOCTEXT("SetFunctionOutputTransaction", "Set function output"));
	Function->Modify();
	Function->OutputNode = NodeId;
	Function->OutputPin = PinId;
	Function->OutputType = Type;
	Function->MarkPackageDirty();
	FVoxelLandEditorOperatorRegistry::NotifyFunctionChanged();
	Status = TEXT("Function output updated. Calling graphs will recompile when the function changes.");
}

TSharedRef<SWidget> FVoxelLandGraphToolkit::BuildPreviewTargetMenu()
{
	FMenuBuilder Menu(true, nullptr);
	bool bHasOutput = false;
	if (GraphWidget && !bStopped && !bPIE && !bReloadPaused)
	{
		for (UObject* Object : GraphWidget->GetSelectedNodes())
		{
			const UVoxelLandEditorNode* Node = Cast<UVoxelLandEditorNode>(Object);
			if (!Node)
			{
				continue;
			}
			for (const UEdGraphPin* Pin : Node->Pins)
			{
				if (!Pin || Pin->Direction != EGPD_Output)
				{
					continue;
				}
				bHasOutput = true;
				const FString Label = FString::Printf(TEXT("%s - %s (%s)"),
					*Node->GetNodeTitle(ENodeTitleType::ListView).ToString(), *Node->GetPinDisplayName(Pin).ToString(),
					*Pin->PinType.PinCategory.ToString());
				Menu.AddMenuEntry(FText::FromString(Label),
					LOCTEXT("PreviewOutputTip", "Preview this output as a signed density field."),
					FSlateIcon(), FUIAction(FExecuteAction::CreateSP(this, &FVoxelLandGraphToolkit::SelectPreviewTarget,
						Node->NodeGuid, Pin->PinId, Label)));
			}
		}
	}
	if (!bHasOutput)
	{
		Menu.AddMenuEntry(LOCTEXT("NoPreviewOutput", "Select a node with an output"), FText::GetEmpty(), FSlateIcon(),
			FUIAction(FExecuteAction(), FCanExecuteAction::CreateLambda([] { return false; })));
	}
	return Menu.MakeWidget();
}

void FVoxelLandGraphToolkit::SelectPreviewTarget(FGuid NodeId, FGuid PinId, FString Label)
{
	if (bStopped || bPIE || bReloadPaused)
	{
		return;
	}
	bM4Preview = false;
	ClearM4StageMap();
	bNoisePreview = false;
	PreviewNodeId = NodeId;
	PreviewPinId = PinId;
	PreviewTargetLabel = MoveTemp(Label);
	++PreviewTargetRevision;
	NodePreviewSnapshot.Reset();
	NodePreviewPayload.Reset();
	SemanticChangeTime = FPlatformTime::Seconds();
	RequestCompile();
}

FReply FVoxelLandGraphToolkit::SelectNoisePreview(bool bStandalone)
{
	if (bStopped || bPIE || bReloadPaused || !GraphWidget) { return FReply::Handled(); }
	bM4Preview = false;
	ClearM4StageMap();
	if (Asset && Asset->IsA<UVoxelLandGraphFunction>())
	{
		CancelPreview();
		Status = TEXT(
			"Error; preview stale: 2D noise preview requires a terrain graph; function noise seeds depend on the calling instance.");
		return FReply::Handled();
	}
	const FGraphPanelSelectionSet Selection = GraphWidget->GetSelectedNodes();
	const UVoxelLandEditorNode* Node = Selection.Num() == 1
		                                   ? Cast<UVoxelLandEditorNode>(*Selection.CreateConstIterator())
		                                   : nullptr;
	FVoxelLandBuiltinNodeSpec NoiseSpec;
	if (!Node || !FVoxelLandBuiltinCatalog::Find(EVoxelLandBuiltinNode(Node->Kind), NoiseSpec) || !NoiseSpec.bNoise)
	{
		CancelPreview();
		Status = TEXT("Error; preview stale: select exactly one Perlin, fBm, Value, or Simplex noise node.");
		return FReply::Handled();
	}
	const UEdGraphPin* Output = nullptr;
	for (const UEdGraphPin* Pin : Node->Pins)
	{
		if (Pin && Pin->Direction == EGPD_Output && Pin->PinType.PinCategory == TEXT("float"))
		{
			if (Output)
			{
				CancelPreview();
				Status = TEXT("Error; preview stale: selected noise node has more than one float output.");
				return FReply::Handled();
			}
			Output = Pin;
		}
	}
	if (!Output)
	{
		CancelPreview();
		Status = TEXT("Error; preview stale: selected noise node has no float output.");
		return FReply::Handled();
	}
	bNoisePreview = true;
	bNoiseStandalone = bStandalone;
	PreviewNodeId = Node->NodeGuid;
	PreviewPinId = Output->PinId;
	PreviewTargetLabel = Node->GetNodeTitle(ENodeTitleType::ListView).ToString();
	++PreviewTargetRevision;
	NodePreviewSnapshot.Reset();
	NodePreviewPayload.Reset();
	SemanticChangeTime = FPlatformTime::Seconds();
	RequestCompile();
	return FReply::Handled();
}

void FVoxelLandGraphToolkit::SetNoiseSpacing(float Value, ETextCommit::Type)
{
	if (!FMath::IsFinite(Value) || Value < 0.01f || Value > 100000.0f)
	{
		bNoiseSpacingValid = false;
		CancelPreview();
		Status = TEXT("Error; preview stale: spacing must be finite and between 0.01 and 100000 cm/pixel.");
		return;
	}
	const bool bChanged = NoiseSpacingCm != Value || !bNoiseSpacingValid;
	bNoiseSpacingValid = true;
	NoiseSpacingCm = Value;
	if (bChanged && bNoisePreview && bNoiseSliceValid)
	{
		SemanticChangeTime = FPlatformTime::Seconds();
		StartPreview();
	}
}

void FVoxelLandGraphToolkit::SetNoiseSlice(float Value, ETextCommit::Type)
{
	if (!FMath::IsFinite(Value) || FMath::Abs(Value) > 10000000.0f)
	{
		bNoiseSliceValid = false;
		CancelPreview();
		Status = TEXT("Error; preview stale: world Z must be finite and within +/-10000000 cm.");
		return;
	}
	const bool bChanged = NoiseSliceZCm != Value || !bNoiseSliceValid;
	bNoiseSliceValid = true;
	NoiseSliceZCm = Value;
	if (bChanged && bNoisePreview && bNoiseSpacingValid)
	{
		SemanticChangeTime = FPlatformTime::Seconds();
		StartPreview();
	}
}

const FSlateBrush* FVoxelLandGraphToolkit::GetNoiseBrush() const
{
	return &NoiseBrush;
}

FReply FVoxelLandGraphToolkit::ReturnToRootPreview()
{
	if (!bStopped && !bPIE && !bReloadPaused && (PreviewNodeId.IsValid() || bM4Preview))
	{
		bM4Preview = false;
		ClearM4StageMap();
		bNoisePreview = false;
		PreviewNodeId.Invalidate();
		PreviewPinId.Invalidate();
		PreviewTargetLabel.Reset();
		++PreviewTargetRevision;
		NodePreviewSnapshot.Reset();
		NodePreviewPayload.Reset();
		SemanticChangeTime = FPlatformTime::Seconds();
		RequestCompile();
	}
	return FReply::Handled();
}

bool FVoxelLandGraphToolkit::BuildNodePreviewSnapshot(FVoxelLandGraphSnapshot& Snapshot, FString& Error) const
{
	const FVoxelLandGraphNodeRecord* Node = Snapshot.Nodes.FindByPredicate(
		[this](const FVoxelLandGraphNodeRecord& Candidate) {
			return Candidate.Id == PreviewNodeId;
		});
	if (!Node)
	{
		Error = TEXT("Selected preview node no longer exists.");
		return false;
	}
	const FVoxelLandGraphPinRecord* Pin = Node->Pins.FindByPredicate([this](const FVoxelLandGraphPinRecord& Candidate) {
		return Candidate.Id == PreviewPinId && Candidate.bOutput;
	});
	if (!Pin)
	{
		Error = TEXT("Selected preview output no longer exists.");
		return false;
	}
	if (Pin->Type != EVoxelLandGraphValueType::Float)
	{
		Error = TEXT(
			"Node preview supports float outputs only; integer and boolean outputs cannot be displayed as density.");
		return false;
	}
	if (Node->OperatorId == VoxelLandBuiltinNodeId(EVoxelLandBuiltinNode::SmoothDensity))
	{
		Error = TEXT("Filter outputs cannot be selected for node preview; preview the graph output instead.");
		return false;
	}
	const auto Provider = FVoxelLandOperatorRegistry::Resolve(Node->OperatorId, Node->OperatorVersion);
	if (Provider && Provider->Stage() == EVoxelLandOperatorStage::OneCellFilter)
	{
		Error = TEXT("Filter outputs cannot be selected for node preview; preview the graph output instead.");
		return false;
	}
	TArray<FGuid> CoordinatePins;
	if (bNoisePreview)
	{
		const FGuid FirstNoise = VoxelLandBuiltinNodeId(EVoxelLandBuiltinNode::Perlin2);
		FVoxelLandBuiltinNodeSpec NoiseSpec;
		if (Node->OperatorId.A != FirstNoise.A || Node->OperatorId.B != FirstNoise.B || Node->OperatorId.C != FirstNoise
		    .C ||
		    !FVoxelLandBuiltinCatalog::Find(EVoxelLandBuiltinNode(Node->OperatorId.D), NoiseSpec) || !NoiseSpec.bNoise)
		{
			Error = TEXT("The selected node is no longer a supported noise node.");
			return false;
		}
		if (bNoiseStandalone)
		{
			const int32 Dimensions = NoiseSpec.NoiseDimensions;
			if (Node->Pins.Num() < Dimensions)
			{
				Error = TEXT("The noise node is missing coordinate inputs.");
				return false;
			}
			for (int32 Index = 0; Index < Dimensions; ++Index)
			{
				if (Node->Pins[Index].bOutput || Node->Pins[Index].Type != EVoxelLandGraphValueType::Float)
				{
					Error = TEXT("The noise node has invalid coordinate inputs.");
					return false;
				}
				CoordinatePins.Add(Node->Pins[Index].Id);
			}
		}
	}
	TSet<FGuid> RemovedOutputs;
	for (const FVoxelLandGraphNodeRecord& Candidate : Snapshot.Nodes)
	{
		if (Candidate.OperatorId == VoxelLandBuiltinNodeId(EVoxelLandBuiltinNode::DensityOutput))
		{
			RemovedOutputs.Add(Candidate.Id);
		}
	}
	Snapshot.Nodes.RemoveAll([&RemovedOutputs](const FVoxelLandGraphNodeRecord& Candidate) {
		return RemovedOutputs.Contains(Candidate.Id);
	});
	Snapshot.Links.RemoveAll([&RemovedOutputs, &CoordinatePins](const FVoxelLandGraphLinkRecord& Link) {
		return RemovedOutputs.Contains(Link.SourceNode) || RemovedOutputs.Contains(Link.TargetNode) || CoordinatePins.
		       Contains(Link.TargetPin);
	});
	TSet<FGuid> ExistingIds;
	for (const FVoxelLandGraphNodeRecord& Candidate : Snapshot.Nodes)
	{
		ExistingIds.Add(Candidate.Id);
		for (const FVoxelLandGraphPinRecord& CandidatePin : Candidate.Pins)
		{
			ExistingIds.Add(CandidatePin.Id);
		}
	}
	for (int32 Index = 0; Index < CoordinatePins.Num(); ++Index)
	{
		FVoxelLandGraphNodeRecord Position;
		do { Position.Id = FGuid::NewGuid(); }
		while (!Position.Id.IsValid() || ExistingIds.Contains(Position.Id));
		ExistingIds.Add(Position.Id);
		const EVoxelLandBuiltinNode Kind = Index == 0
			                                   ? EVoxelLandBuiltinNode::PositionX
			                                   : Index == 1
			                                   ? EVoxelLandBuiltinNode::PositionY
			                                   : EVoxelLandBuiltinNode::PositionZ;
		Position.OperatorId = VoxelLandBuiltinNodeId(Kind);
		FVoxelLandGraphPinRecord PositionOutput;
		do { PositionOutput.Id = FGuid::NewGuid(); }
		while (!PositionOutput.Id.IsValid() || ExistingIds.Contains(PositionOutput.Id));
		ExistingIds.Add(PositionOutput.Id);
		PositionOutput.Name = TEXT("Result");
		PositionOutput.Type = EVoxelLandGraphValueType::Float;
		PositionOutput.bOutput = true;
		Position.Pins.Add(PositionOutput);
		FVoxelLandGraphLinkRecord CoordinateLink;
		CoordinateLink.SourceNode = Position.Id;
		CoordinateLink.SourcePin = PositionOutput.Id;
		CoordinateLink.TargetNode = PreviewNodeId;
		CoordinateLink.TargetPin = CoordinatePins[Index];
		Snapshot.Nodes.Add(MoveTemp(Position));
		Snapshot.Links.Add(MoveTemp(CoordinateLink));
	}
	FVoxelLandGraphNodeRecord Output;
	do { Output.Id = FGuid::NewGuid(); }
	while (!Output.Id.IsValid() || ExistingIds.Contains(Output.Id));
	ExistingIds.Add(Output.Id);
	Output.OperatorId = VoxelLandBuiltinNodeId(EVoxelLandBuiltinNode::DensityOutput);
	FVoxelLandGraphPinRecord Input;
	do { Input.Id = FGuid::NewGuid(); }
	while (!Input.Id.IsValid() || ExistingIds.Contains(Input.Id));
	Input.Name = TEXT("Density");
	Input.Type = EVoxelLandGraphValueType::Float;
	Output.Pins.Add(Input);
	FVoxelLandGraphLinkRecord Link;
	Link.SourceNode = PreviewNodeId;
	Link.SourcePin = PreviewPinId;
	Link.TargetNode = Output.Id;
	Link.TargetPin = Input.Id;
	Snapshot.Nodes.Add(MoveTemp(Output));
	Snapshot.Links.Add(MoveTemp(Link));
	return true;
}

void FVoxelLandGraphToolkit::CreateMirror()
{
	bLoading = true;
	if (Asset->EditorGraph)
	{
		Graph = Asset->EditorGraph;
		bLoading = false;
		return;
	}
	Asset->Modify();
	Graph = NewObject<UEdGraph>(Asset, NAME_None, RF_Transactional);
	Graph->Schema = UVoxelLandEditorSchema::StaticClass();
	Asset->EditorGraph = Graph;
	TMap<FGuid, UEdGraphPin*> PinsById;
	for (const FVoxelLandGraphNodeRecord& Record : Asset->Nodes)
	{
		UVoxelLandEditorNode* Node = NewObject<UVoxelLandEditorNode>(Graph, NAME_None, RF_Transactional);
		Node->NodeGuid = Record.Id;
		if (Record.OperatorId.A == 0x564c4d33 && Record.OperatorId.B == 1 && Record.OperatorId.C == 0)
		{
			Node->Kind = int32(Record.OperatorId.D);
		}
		else
		{
			Node->Kind = 0;
			Node->ExternalOperatorId = Record.OperatorId;
			Node->ExternalPins = Record.Pins;
			TArray<FVoxelLandEditorOperatorPresentation> Presentations;
			FVoxelLandEditorOperatorRegistry::GetPresentations(Presentations);
			for (const FVoxelLandEditorOperatorPresentation& Presentation : Presentations)
			{
				if (Presentation.OperatorId == Record.OperatorId && Presentation.Version == Record.OperatorVersion)
				{
					Node->ExternalTitle = Presentation.DisplayName;
					break;
				}
			}
		}
		Node->OperatorVersion = Record.OperatorVersion;
		Node->ParameterId = Record.ParameterId;
		Node->SeedSalt = Record.SeedSalt;
		Node->Configuration = Record.Configuration;
		Node->FunctionPath = Record.FunctionPath;
		if (Node->Kind == int32(VoxelLandGraphFunctionCallId().D))
		{
			Node->ExternalPins = Record.Pins;
			if (const UVoxelLandGraphFunction* Function = Cast<UVoxelLandGraphFunction>(Node->FunctionPath.TryLoad()))
			{
				Node->ExternalTitle = FText::FromString(Function->GetName());
			}
		}
		Node->VariadicInputs = FMath::Max(2, Record.Pins.Num() - 1);
		Graph->AddNode(Node, false, false);
		for (const FVoxelLandGraphPinRecord& Pin : Record.Pins)
		{
			RestorePin(*Node, Pin);
			PinsById.Add(Pin.Id, Node->Pins.Last());
		}
	}
	for (const FVoxelLandGraphLinkRecord& Link : Asset->Links)
	{
		UEdGraphPin* const* Source = PinsById.Find(Link.SourcePin);
		UEdGraphPin* const* Target = PinsById.Find(Link.TargetPin);
		if (Source && Target)
		{
			(*Source)->MakeLinkTo(*Target);
		}
	}
	if (Asset->Nodes.IsEmpty() && !Asset->IsA<UVoxelLandGraphFunction>())
	{
		UVoxelLandM4ControlGraph* Named = Cast<UVoxelLandM4ControlGraph>(Asset);
		const int32 Count = Named ? 3 : 1;
		for (int32 Index = 0; Index < Count; ++Index)
		{
			UVoxelLandEditorNode* Output = NewObject<UVoxelLandEditorNode>(Graph, NAME_None, RF_Transactional);
			Output->Kind = int32(EVoxelLandBuiltinNode::DensityOutput);
			Output->CreateNewGuid();
			Output->NodePosY = Index * 180;
			Graph->AddNode(Output, false, false);
			Output->AllocateDefaultPins();
			if (Named)
			{
				if (Index == 0)
				{
					Named->ContinentalOutputId = Output->NodeGuid;
				}
				else if (Index == 1)
				{
					Named->MountainOutputId = Output->NodeGuid;
				}
				else
				{
					Named->PeaksOutputId = Output->NodeGuid;
				}
			}
		}
	}
	Asset->MarkPackageDirty();
	bLoading = false;
}

bool FVoxelLandGraphToolkit::CaptureMirrorSnapshot(FVoxelLandGraphSnapshot& Snapshot, bool& bValidDefaults,
	FString& Error, FVoxelLandGraphDiagnostic* Diagnostic) const
{
	Snapshot = FVoxelLandGraphSnapshot();
	bValidDefaults = true;
	if (Diagnostic) { *Diagnostic = FVoxelLandGraphDiagnostic(); }
	if (!Asset || !Graph)
	{
		Error = TEXT("Missing graph asset or editor mirror.");
		return false;
	}
	Snapshot.SourceVersion = Asset->SourceVersion;
	Snapshot.Parameters = Asset->Parameters;
	for (UEdGraphNode* GraphNode : Graph->Nodes)
	{
		const UVoxelLandEditorNode* Node = Cast<UVoxelLandEditorNode>(GraphNode);
		if (Node && !CaptureNode(*Node, Snapshot.Nodes, Snapshot.Links, bValidDefaults, Error, Diagnostic))
		{
			return false;
		}
	}
	return true;
}

bool FVoxelLandGraphToolkit::CaptureMirrorWithDependencies(FVoxelLandGraphSnapshot& Snapshot, bool& bValidDefaults,
	FString& Error, FVoxelLandGraphDiagnostic* Diagnostic) const
{
	if (!CaptureMirrorSnapshot(Snapshot, bValidDefaults, Error, Diagnostic))
	{
		return false;
	}
	FVoxelLandGraphDiagnostic DependencyDiagnostic;
	if (!FVoxelLandGraphFunctionCapture::Capture(Snapshot, DependencyDiagnostic))
	{
		Error = DependencyDiagnostic.Message;
		if (Diagnostic) { *Diagnostic = MoveTemp(DependencyDiagnostic); }
		return false;
	}
	return true;
}

bool FVoxelLandGraphToolkit::CaptureMirror(FString& Error, FVoxelLandGraphDiagnostic* Diagnostic)
{
	FVoxelLandGraphSnapshot Snapshot;
	bool bValidDefaults = true;
	if (!CaptureMirrorSnapshot(Snapshot, bValidDefaults, Error, Diagnostic))
	{
		return false;
	}
	const uint64 MirrorHash = FVoxelLandProgramCodec::SourceFingerprint(Snapshot);
	FVoxelLandGraphSnapshot Stored;
	Stored.SourceVersion = Asset->SourceVersion;
	Stored.Parameters = Asset->Parameters;
	Stored.Nodes = Asset->Nodes;
	Stored.Links = Asset->Links;
	const bool bSemanticSourceChanged = FVoxelLandProgramCodec::SourceFingerprint(Stored) != MirrorHash;
	if (bSemanticSourceChanged || !SamePinNames(Stored.Nodes, Snapshot.Nodes))
	{
		if (!GIsTransacting)
		{
			Asset->Modify();
		}
		Asset->Nodes = Snapshot.Nodes;
		Asset->Links = Snapshot.Links;
		Asset->MarkPackageDirty();
		// Function dependencies already propagate through the registry revision;
		// this signal handles direct control graphs in other open toolkits.
		if (bSemanticSourceChanged && !Asset->IsA<UVoxelLandGraphFunction>())
		{
			M4ControlGraphChanged.Broadcast(Asset);
		}
		if (Asset->IsA<UVoxelLandGraphFunction>())
		{
			FVoxelLandEditorOperatorRegistry::NotifyFunctionChanged();
		}
	}
	FVoxelLandGraphDiagnostic DependencyDiagnostic;
	if (!FVoxelLandGraphFunctionCapture::Capture(Snapshot, DependencyDiagnostic))
	{
		Error = DependencyDiagnostic.Message;
		if (Diagnostic) { *Diagnostic = MoveTemp(DependencyDiagnostic); }
		return false;
	}
	const uint64 NewHash = FVoxelLandProgramCodec::SourceFingerprint(Snapshot);
	if (NewHash != LastSourceHash)
	{
		LastSourceHash = NewHash;
		++Revision;
		Status = TEXT("Stale: graph changed.");
	}
	return bValidDefaults;
}

void FVoxelLandGraphToolkit::PostUndo(bool bSuccess)
{
	if (bSuccess && !bStopped)
	{
		if (Asset && Asset->IsA<UVoxelLandGraphFunction>())
		{
			FVoxelLandEditorOperatorRegistry::NotifyFunctionChanged();
		}
		const uint64 PreviousRevision = Revision;
		SynchronizeGraph();
		SynchronizePreviewSettings(true);
		if (bM4Preview && M4PreviewConfig)
		{
			const bool bEditedAssetIsControl =
				Asset == M4PreviewConfig->Continental.Graph || Asset == M4PreviewConfig->Mountain.Graph
				|| Asset == M4PreviewConfig->Peaks.Graph
				|| M4FunctionDependencies.ContainsByPredicate(
					[this](const TWeakObjectPtr<UObject>& Item) { return Item.Get() == Asset; });
			if (bM4UndoMaterialChange)
			{
				const UVoxelLandMaterialSetup* Setup = M4PreviewConfig->MaterialSetup.LoadSynchronous();
				ApplyPreviewMaterial(Setup && Setup->FormatVersion == 1 ? Setup->Material.LoadSynchronous() : nullptr);
			}
			if (bM4UndoSemanticChange || (bEditedAssetIsControl && PreviousRevision != Revision))
			{
				StartM4Preview();
			}
		}
		bM4UndoSemanticChange = false;
		bM4UndoMaterialChange = false;
		if (PreviewDetails)
		{
			PreviewDetails->ForceRefresh();
		}
		if (GraphWidget && Graph)
		{
			const TSet<UObject*> Selection = GraphWidget->GetSelectedNodes();
			for (UObject* Object : Selection)
			{
				UEdGraphNode* Node = Cast<UEdGraphNode>(Object);
				if (Node && !Graph->Nodes.Contains(Node))
				{
					GraphWidget->SetNodeSelection(Node, false);
				}
			}
			TGuardValue<bool> Guard(bLoading, true);
			GraphWidget->NotifyGraphChanged();
#if WITH_DEV_AUTOMATION_TESTS
			++PanelRefreshCountForTests;
#endif
		}
	}
	else
	{
		bM4UndoSemanticChange = false;
		bM4UndoMaterialChange = false;
	}
}

void FVoxelLandGraphToolkit::PostRedo(bool bSuccess)
{
	PostUndo(bSuccess);
}

void FVoxelLandGraphToolkit::GraphChanged(const FEdGraphEditAction&)
{
	if (bLoading || GIsTransacting)
	{
		return;
	}
	SynchronizeGraph();
}

void FVoxelLandGraphToolkit::SynchronizeGraph()
{
	const uint64 Previous = Revision;
	FString Error;
	FVoxelLandGraphDiagnostic Diagnostic;
	if (!CaptureMirror(Error, &Diagnostic))
	{
		ErrorDiagnostic = MoveTemp(Diagnostic);
		CancelPreview();
		CompileDue = 0.0;
		++Revision;
		Status = FString::Printf(TEXT("Error; preview stale: %s"), *Error);
		return;
	}
	if (Previous != Revision)
	{
		SemanticChangeTime = FPlatformTime::Seconds();
		ErrorDiagnostic = FVoxelLandGraphDiagnostic();
		CancelPreview();
		CompileDue = FPlatformTime::Seconds() + 0.2;
	}
}

void FVoxelLandGraphToolkit::PropertiesChanged(const FPropertyChangedEvent&)
{
	if (Asset && Asset->IsA<UVoxelLandGraphFunction>())
	{
		FVoxelLandEditorOperatorRegistry::NotifyFunctionChanged();
	}
	if (PreviewDetails)
	{
		PreviewDetails->ForceRefresh();
	}
	GraphChanged(FEdGraphEditAction());
}

void FVoxelLandGraphToolkit::SelectionChanged(const TSet<UObject*>& Selection)
{
	if (Selection.Num() == 1)
	{
		Details->SetObject(*Selection.CreateConstIterator());
	}
	else
	{
		Details->SetObject(Asset);
	}
}

void FVoxelLandGraphToolkit::RequestCompile()
{
	if (bStopped || bPIE || bReloadPaused)
	{
		return;
	}
	CompileDue = 0.0;
	if (Asset && Asset->IsA<UVoxelLandGraphFunction>())
	{
		CancelPreview();
		Status = TEXT("Function source: choose an output and preview it through a calling graph.");
		return;
	}
	if (SemanticChangeTime <= 0.0)
	{
		SemanticChangeTime = FPlatformTime::Seconds();
	}
	CancelPreview();
	if (CompileWork)
	{
		bCompileRequested = true;
		Status = TEXT("Compiling; prior preview is stale.");
		return;
	}
	FString Error;
	FVoxelLandGraphDiagnostic Diagnostic;
	if (!PreviewNodeId.IsValid() && !CaptureMirror(Error, &Diagnostic))
	{
		M4ClearNamedPrograms(Cast<UVoxelLandM4ControlGraph>(Asset));
		ErrorDiagnostic = MoveTemp(Diagnostic);
		Status = FString::Printf(TEXT("Error; preview stale: %s"), *Error);
		return;
	}
	if (!PreviewNodeId.IsValid())
	{
		if (UVoxelLandM4ControlGraph* Named = Cast<UVoxelLandM4ControlGraph>(Asset))
		{
			FVoxelLandGraphSnapshot Source;
			FVoxelLandGraphSnapshot Projections[3];
			const EVoxelLandM4ControlChannel Channels[] = {EVoxelLandM4ControlChannel::Continental,
				EVoxelLandM4ControlChannel::Mountain, EVoxelLandM4ControlChannel::Peaks};
			bool bCaptured = Named->CaptureSnapshot(Source, Error);
			for (int32 I = 0; bCaptured && I < 3; ++I)
			{
				bCaptured = Named->ProjectSnapshot(Source, Channels[I], Projections[I], Error);
			}
			if (!bCaptured)
			{
				M4ClearNamedPrograms(Named);
				Status = FString::Printf(TEXT("Error; M4 preview stale: %s"), *Error);
				return;
			}
			const TSharedRef<FVoxelLandCompileWork, ESPMode::ThreadSafe> Work =
				MakeShared<FVoxelLandCompileWork, ESPMode::ThreadSafe>();
			Work->Revision = Revision;
			Work->TargetRevision = PreviewTargetRevision;
			Work->SourceHash = FVoxelLandProgramCodec::SourceFingerprint(Source);
			Work->bNamedM4 = true;
			for (int32 I = 0; I < 3; ++I)
			{
				Work->NamedOutputIds[I] = Named->GetOutputId(Channels[I]);
			}
			CompileWork = Work;
			Status = TEXT("Compiling M4 control outputs; prior preview is stale.");
			WorkerTasks.Add(Async(EAsyncExecution::ThreadPool,
				[P0 = MoveTemp(Projections[0]), P1 = MoveTemp(Projections[1]), P2 = MoveTemp(Projections[2]), Work]() {
					const double Started = FPlatformTime::Seconds();
					const FVoxelLandGraphSnapshot* Inputs[] = {&P0, &P1, &P2};
					Work->bSuccess = true;
					for (int32 I = 0; I < 3; ++I)
					{
						FVoxelLandProgramDraft Draft;
						TSharedPtr<const FVoxelLandProgram, ESPMode::ThreadSafe> Program;
						if (!FVoxelLandGraphCompiler::Compile(*Inputs[I], Draft, Work->Error))
						{
							Work->bSuccess = false;
							break;
						}
						if (const FVoxelLandError Failure = FVoxelLandProgram::Validate(Draft, Program))
						{
							Work->Error = Failure.Message;
							Work->bSuccess = false;
							break;
						}
						if (const FVoxelLandError Failure = VoxelLandM4ControlGrid::ValidateProgram(*Program))
						{
							Work->Error = Failure.Message;
							Work->bSuccess = false;
							break;
						}
						if (!FVoxelLandProgramCodec::Pack(*Inputs[I], *Program, Work->NamedPrograms[I], Work->Error))
						{
							Work->bSuccess = false;
							break;
						}
					}
					Work->CompileMs = (FPlatformTime::Seconds() - Started) * 1000.0;
					Work->Done.Store(true);
				}));
			return;
		}
	}
	FVoxelLandGraphSnapshot Snapshot;
	bool bValidDefaults = true;
	const bool bSnapshotValid = PreviewNodeId.IsValid()
		                            ? CaptureMirrorWithDependencies(Snapshot, bValidDefaults, Error, &Diagnostic) &&
		                              bValidDefaults
		                            : Asset->CaptureSnapshot(Snapshot, Diagnostic);
	if (!bSnapshotValid)
	{
		ErrorDiagnostic = MoveTemp(Diagnostic);
		Status = FString::Printf(TEXT("Error; preview stale: %s"),
			*(ErrorDiagnostic.Message.IsEmpty() ? Error : ErrorDiagnostic.Message));
		return;
	}
	const uint64 SourceHash = FVoxelLandProgramCodec::SourceFingerprint(Snapshot);
	const bool bNodePreview = PreviewNodeId.IsValid();
	if (bNodePreview && !BuildNodePreviewSnapshot(Snapshot, Error))
	{
		NodePreviewSnapshot.Reset();
		NodePreviewPayload.Reset();
		ErrorDiagnostic = FVoxelLandGraphDiagnostic();
		Status = FString::Printf(TEXT("Error; preview stale: %s"), *Error);
		return;
	}
	ErrorDiagnostic = FVoxelLandGraphDiagnostic();
	Status = TEXT("Compiling; prior preview is stale.");
	const TSharedRef<FVoxelLandCompileWork, ESPMode::ThreadSafe> Work = MakeShared<FVoxelLandCompileWork,
		ESPMode::ThreadSafe>();
	Work->Revision = Revision;
	Work->TargetRevision = PreviewTargetRevision;
	Work->SourceHash = SourceHash;
	Work->bNodePreview = bNodePreview;
	if (bNodePreview)
	{
		Work->Snapshot = Snapshot;
		NodePreviewSnapshot.Reset();
		NodePreviewPayload.Reset();
	}
	CompileWork = Work;
	WorkerTasks.Add(Async(EAsyncExecution::ThreadPool, [Snapshot = MoveTemp(Snapshot), Work]() mutable {
		const double StartTime = FPlatformTime::Seconds();
		const FVoxelLandOperatorRegistrySnapshot Providers = FVoxelLandOperatorRegistry::Snapshot();
		FVoxelLandProgramDraft Draft;
		Work->bSuccess = FVoxelLandGraphCompiler::Compile(Snapshot, Providers, Draft, Work->Diagnostic);
		if (!Work->bSuccess)
		{
			Work->Error = Work->Diagnostic.Message;
		}
		if (Work->bSuccess)
		{
			TSharedPtr<const FVoxelLandProgram, ESPMode::ThreadSafe> Program;
			const FVoxelLandError Validation = FVoxelLandProgram::Validate(Draft, Providers, Program);
			if (Validation)
			{
				Work->Error = Validation.Message;
				Work->bSuccess = false;
			}
			else
			{
				Work->bSuccess = FVoxelLandProgramCodec::Pack(Snapshot, *Program, Work->Payload, Work->Error);
			}
		}
		Work->CompileMs = (FPlatformTime::Seconds() - StartTime) * 1000.0;
		Work->Done.Store(true);
	}));
}

void FVoxelLandGraphToolkit::BindCommands()
{
	FGraphEditorCommands::Register();
	Commands = MakeShared<FUICommandList>();
	Commands->MapAction(FGenericCommands::Get().Delete,
		FExecuteAction::CreateSP(this, &FVoxelLandGraphToolkit::DeleteSelected));
	Commands->MapAction(FGenericCommands::Get().Cut,
		FExecuteAction::CreateSP(this, &FVoxelLandGraphToolkit::CutSelected));
	Commands->MapAction(FGraphEditorCommands::Get().CreateComment,
		FExecuteAction::CreateSP(this, &FVoxelLandGraphToolkit::CreateComment));
	Commands->MapAction(FGenericCommands::Get().Copy,
		FExecuteAction::CreateSP(this, &FVoxelLandGraphToolkit::CopySelected));
	Commands->MapAction(FGenericCommands::Get().Paste,
		FExecuteAction::CreateSP(this, &FVoxelLandGraphToolkit::PasteNodes));
	Commands->MapAction(FGenericCommands::Get().Duplicate,
		FExecuteAction::CreateSP(this, &FVoxelLandGraphToolkit::DuplicateSelected));
}

void FVoxelLandGraphToolkit::DeleteSelected()
{
	if (!GraphWidget)
	{
		return;
	}
	const FScopedTransaction Transaction(LOCTEXT("Delete", "Delete graph nodes"));
	Graph->Modify();
	const FGraphPanelSelectionSet Selection = GraphWidget->GetSelectedNodes();
	GraphWidget->ClearSelectionSet();
	for (UObject* Object : Selection)
	{
		UEdGraphNode* Node = Cast<UEdGraphNode>(Object);
		if (Node && Node->CanUserDeleteNode())
		{
			Node->Modify();
			Node->DestroyNode();
		}
	}
	Graph->NotifyGraphChanged();
}

void FVoxelLandGraphToolkit::CopySelected()
{
	if (!GraphWidget)
	{
		return;
	}
	FGraphPanelSelectionSet Copyable;
	for (UObject* Object : GraphWidget->GetSelectedNodes())
	{
		UEdGraphNode* Node = Cast<UEdGraphNode>(Object);
		if (Node && Node->CanDuplicateNode())
		{
			Copyable.Add(Node);
		}
	}
	if (!Copyable.IsEmpty())
	{
		FString Exported;
		FEdGraphUtilities::ExportNodesToText(Copyable, Exported);
		FPlatformApplicationMisc::ClipboardCopy(*Exported);
	}
}

void FVoxelLandGraphToolkit::CutSelected()
{
	if (!GraphWidget || !Graph)
	{
		return;
	}
	FGraphPanelSelectionSet Cuttable;
	for (UObject* Object : GraphWidget->GetSelectedNodes())
	{
		UEdGraphNode* Node = Cast<UEdGraphNode>(Object);
		if (Node && Node->CanUserDeleteNode() && Node->CanDuplicateNode())
		{
			Cuttable.Add(Node);
		}
	}
	if (Cuttable.IsEmpty())
	{
		return;
	}
	FString Exported;
	FEdGraphUtilities::ExportNodesToText(Cuttable, Exported);
	if (Exported.IsEmpty())
	{
		return;
	}
	FPlatformApplicationMisc::ClipboardCopy(*Exported);
	const FScopedTransaction Transaction(LOCTEXT("Cut", "Cut graph nodes"));
	Graph->Modify();
	for (UObject* Object : Cuttable)
	{
		UEdGraphNode* Node = CastChecked<UEdGraphNode>(Object);
		GraphWidget->SetNodeSelection(Node, false);
		Node->Modify();
		Node->DestroyNode();
	}
	Graph->NotifyGraphChanged();
}

void FVoxelLandGraphToolkit::CreateComment()
{
	if (!GraphWidget || !Graph)
	{
		return;
	}
	UEdGraphNode_Comment* Template = NewObject<UEdGraphNode_Comment>();
	FSlateRect Bounds;
	FVector2f Location = GraphWidget->GetPasteLocation2f();
	if (GraphWidget->GetBoundsForSelectedNodes(Bounds, 50.0f))
	{
		Template->SetBounds(Bounds);
		Location = FVector2f(Template->NodePosX, Template->NodePosY);
	}
	FEdGraphSchemaAction_NewNode::SpawnNodeFromTemplate<UEdGraphNode_Comment>(Graph, Template, Location);
}

void FVoxelLandGraphToolkit::FrameSelection()
{
	if (GraphWidget)
	{
		GraphWidget->ZoomToFit(true);
	}
}

void FVoxelLandGraphToolkit::ExtendGraphToolbar()
{
	TSharedPtr<FExtender> Extender = MakeShared<FExtender>();
	const FUIAction CompileAction(FExecuteAction::CreateSP(this, &FVoxelLandGraphToolkit::RequestCompile));
	const FUIAction FrameAction(FExecuteAction::CreateSP(this, &FVoxelLandGraphToolkit::FrameSelection));
	Extender->AddToolBarExtension("Asset", EExtensionHook::After, GetToolkitCommands(),
		FToolBarExtensionDelegate::CreateLambda([CompileAction, FrameAction](FToolBarBuilder& Builder) {
			Builder.AddToolBarButton(CompileAction, NAME_None, LOCTEXT("ToolbarCompile", "Compile"),
				LOCTEXT("ToolbarCompileTip", "Compile the terrain graph."), FSlateIcon(FAppStyle::GetAppStyleSetName(),
					"AssetEditor.CompileStatus.Background"));
			Builder.AddToolBarButton(FrameAction, NAME_None, LOCTEXT("ToolbarFrame", "Frame selection"),
				LOCTEXT("ToolbarFrameTip", "Frame selected graph nodes."),
				FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.FrameActor"));
		}));
	AddToolbarExtender(Extender);
}

void FVoxelLandGraphToolkit::PasteNodes()
{
	if (!GraphWidget)
	{
		return;
	}
	FString Text;
	FPlatformApplicationMisc::ClipboardPaste(Text);
	if (!FEdGraphUtilities::CanImportNodesFromText(Graph, Text))
	{
		return;
	}
	const FScopedTransaction Transaction(LOCTEXT("Paste", "Paste graph nodes"));
	Graph->Modify();
	GraphWidget->ClearSelectionSet();
	TSet<UEdGraphNode*> Imported;
	FEdGraphUtilities::ImportNodesFromText(Graph, Text, Imported);
	for (UEdGraphNode* Node : Imported)
	{
		Node->SetFlags(RF_Transactional);
		Node->Modify();
		Node->CreateNewGuid();
		// ImportNodesFromText resolves internal pin links before returning. Reassign
		// IDs only after that pass; links themselves are retained as pin pointers.
		for (UEdGraphPin* Pin : Node->Pins)
		{
			if (Pin)
			{
				Pin->Modify();
				const FGuid OldPinId = Pin->PinId;
				Pin->PinId = FGuid::NewGuid();
				if (UVoxelLandEditorNode* TerrainNode = Cast<UVoxelLandEditorNode>(Node))
				{
					if (const FGuid* InterfaceId = TerrainNode->FunctionPinIds.Find(OldPinId))
					{
						const FGuid Value = *InterfaceId;
						TerrainNode->FunctionPinIds.Remove(OldPinId);
						TerrainNode->FunctionPinIds.Add(Pin->PinId, Value);
					}
				}
			}
		}
		Node->NodePosX += 32;
		Node->NodePosY += 32;
		GraphWidget->SetNodeSelection(Node, true);
	}
	Graph->NotifyGraphChanged();
}

void FVoxelLandGraphToolkit::DuplicateSelected()
{
	if (!GraphWidget)
	{
		return;
	}
	for (UObject* Object : GraphWidget->GetSelectedNodes())
	{
		const UEdGraphNode* Node = Cast<UEdGraphNode>(Object);
		if (Node && Node->CanDuplicateNode())
		{
			CopySelected();
			PasteNodes();
			return;
		}
	}
}


void FVoxelLandGraphToolkit::CancelPreview()
{
	// A report can describe only the currently accepted inspection view.
	// Semantic edits, cancellation, reload and PIE invalidate it immediately.
	M4InspectionDetails.Reset();
	PreviewFence.Reset();
	++PreviewRevision;
	bPreviewRequested = false;
	if (PreviewCancel)
	{
		PreviewCancel->Store(true);
		PreviewCancel.Reset();
	}
}

void FVoxelLandGraphToolkit::BeginPIE(bool)
{
	bPIE = true;
	++Revision;
	CancelPreview();
	CompileDue = 0.0;
	bCompileRequested = false;
	for (const TFuture<void>& Task : WorkerTasks)
	{
		Task.Wait();
	}
	WorkerTasks.Reset();
	CompileWork.Reset();
	PreviewWork.Reset();
	Status = TEXT("Stale: preview paused during PIE.");
}

void FVoxelLandGraphToolkit::EndPIE(bool)
{
	bPIE = false;
	RequestCompile();
}

void FVoxelLandGraphToolkit::ClearM4StageMap()
{
	M4StageBrush.SetResourceObject(nullptr);
	M4StageTexture = nullptr;
	M4ControlsBrush.SetResourceObject(nullptr);
	M4ControlsTexture = nullptr;
	M4StageDiagnostics.Reset();
	M4InspectionDetails.Reset();
}

void FVoxelLandGraphToolkit::ClearPreview()
{
	ClearM4StageMap();
	if (PreviewViewport)
	{
		PreviewViewport->SetTerrainBounds(FBox(ForceInit));
	}
	if (PreviewScene)
	{
		for (UVoxelLandChunkComponent* Component : PreviewComponents)
		{
			if (Component)
			{
				PreviewScene->RemoveComponent(Component);
			}
		}
	}
	PreviewComponents.Reset();
}

void FVoxelLandGraphToolkit::SeedCommitted(const FText& Text, ETextCommit::Type)
{
	int64 NewSeed = 0;
	if (!LexTryParseString(NewSeed, *Text.ToString()))
	{
		Status = TEXT("Error: seed must be a signed 64-bit integer; preview is stale.");
		CancelPreview();
		return;
	}
	if (NewSeed != PreviewSeed)
	{
		const FScopedTransaction Transaction(LOCTEXT("SetPreviewSeed", "Set preview seed"));
		PreviewSettings->Modify();
		PreviewSettings->Seed = NewSeed;
		SynchronizePreviewSettings(true);
	}
}

void FVoxelLandGraphToolkit::CommitPreviewRadius(int32 Value, bool bHorizontal)
{
	if (!PreviewSettings)
	{
		return;
	}
	const int32 Radius = FMath::Clamp(Value, 1, 2);
	int32& Target = bHorizontal ? PreviewSettings->HorizontalRadius : PreviewSettings->VerticalRadius;
	if (Target == Radius)
	{
		return;
	}
	const FScopedTransaction Transaction(LOCTEXT("SetPreviewRadius", "Set preview radius"));
	PreviewSettings->Modify();
	Target = Radius;
	SynchronizePreviewSettings(true);
}

void FVoxelLandGraphToolkit::PauseForReload(bool bGraphAffected)
{
	if (bStopped) { return; }
	bReloadGraphAffected |= bGraphAffected;
	if (bReloadPaused) { return; }
	bReloadPaused = true;
	StatusBeforeReload = Status;
	bReloadGraphAffected |= CompileWork.IsValid() || bCompileRequested || CompileDue > 0.0;
	bResumePreviewAfterReload = PreviewWork.IsValid() || bPreviewRequested;
	++Revision;
	CancelPreview();
	CompileDue = 0.0;
	bCompileRequested = false;
#if WITH_DEV_AUTOMATION_TESTS
	if (ReloadDrainingForTests)
	{
		ReloadDrainingForTests();
	}
#endif
	for (const TFuture<void>& Task : WorkerTasks) { Task.Wait(); }
	WorkerTasks.Reset();
	CompileWork.Reset();
	PreviewWork.Reset();
	Status = TEXT("Stale: asset reload in progress.");
}

void FVoxelLandGraphToolkit::ResumeAfterReload()
{
	if (bStopped || !bReloadPaused) { return; }
	bReloadPaused = false;
	const bool bCompile = bReloadGraphAffected;
	const bool bPreview = bResumePreviewAfterReload;
	bReloadGraphAffected = false;
	bResumePreviewAfterReload = false;
	SynchronizePreviewSettings(true);
	if (bCompile) { RequestCompile(); }
	else if (bPreview) { StartPreview(); }
	else { Status = StatusBeforeReload; }
	StatusBeforeReload.Reset();
}

void FVoxelLandGraphToolkit::PackageReloaded(EPackageReloadPhase Phase, FPackageReloadedEvent* Event)
{
	if (bStopped) { return; }
	if (Phase == EPackageReloadPhase::PrePackageLoad && Event && Event->GetOldPackage())
	{
		const UPackage* Package = Event->GetOldPackage();
		const bool bGraphAffected = Asset && Asset->GetOutermost() == Package;
		const UObject* Setup = PreviewSettings ? PreviewSettings->MaterialSetup.Get() : nullptr;
		const UObject* Selected = SelectedPreviewSettings.Get();
		const bool bM4FunctionAffected =
			M4FunctionDependencies.ContainsByPredicate([Package](const TWeakObjectPtr<UObject>& Dependency) {
				const UObject* Object = Dependency.Get();
				return Object && Object->GetOutermost() == Package;
			});
		const bool bM4Affected =
			bM4FunctionAffected
			|| M4PreviewConfig
				   && (M4PreviewConfig->GetOutermost() == Package
					   || (M4PreviewConfig->Continental.Graph
						   && M4PreviewConfig->Continental.Graph->GetOutermost() == Package)
					   || (M4PreviewConfig->Mountain.Graph
						   && M4PreviewConfig->Mountain.Graph->GetOutermost() == Package)
					   || (M4PreviewConfig->Peaks.Graph && M4PreviewConfig->Peaks.Graph->GetOutermost() == Package));
		if (bGraphAffected || (Setup && Setup->GetOutermost() == Package)
			|| (Selected && Selected->GetOutermost() == Package)
			|| (PreviewMaterial && PreviewMaterial->GetOutermost() == Package) || bM4Affected)
		{
			bPackageReloadInProgress = true;
			PauseForReload(bGraphAffected);
		}
	}
	else if (Phase == EPackageReloadPhase::PrePackageFixup && Event && Asset && Event->GetOldPackage() == Asset->
	         GetOutermost())
	{
		PauseForReload(true);
		Stop();
		if (bHasEditorWindow) { CloseWindow(EAssetEditorCloseReason::AssetEditorHostClosed); }
	}
	else if (Phase == EPackageReloadPhase::PostBatchPostGC && bPackageReloadInProgress)
	{
		bPackageReloadInProgress = false;
		ResumeAfterReload();
	}
}

void FVoxelLandGraphToolkit::ObjectsReplaced(const TMap<UObject*, UObject*>& Replacements)
{
	if (bStopped) { return; }
	bool bGraphAffected = Replacements.Contains(Asset) || Replacements.Contains(Graph);
	if (!bGraphAffected && Graph)
	{
		for (const UEdGraphNode* Node : Graph->Nodes)
		{
			if (Replacements.Contains(const_cast<UEdGraphNode*>(Node)))
			{
				bGraphAffected = true;
				break;
			}
		}
	}
	const bool bM4FunctionAffected =
		M4FunctionDependencies.ContainsByPredicate([&Replacements](const TWeakObjectPtr<UObject>& Dependency) {
			UObject* Object = Dependency.Get();
			return Object && Replacements.Contains(Object);
		});
	const bool bM4Affected =
		bM4FunctionAffected
		|| M4PreviewConfig
			   && (Replacements.Contains(M4PreviewConfig) || Replacements.Contains(M4PreviewConfig->Continental.Graph)
				   || Replacements.Contains(M4PreviewConfig->Mountain.Graph)
				   || Replacements.Contains(M4PreviewConfig->Peaks.Graph));
	if (bGraphAffected || Replacements.Contains(PreviewSettings) || bM4Affected)
	{
		PauseForReload(true);
		Stop();
		if (bHasEditorWindow) { CloseWindow(EAssetEditorCloseReason::AssetEditorHostClosed); }
		return;
	}
	const UObject* Setup = PreviewSettings ? PreviewSettings->MaterialSetup.Get() : nullptr;
	if ((Setup && Replacements.Contains(const_cast<UObject*>(Setup))) || Replacements.Contains(PreviewMaterial) ||
	    (SelectedPreviewSettings.IsValid() && Replacements.Contains(SelectedPreviewSettings.Get())))
	{
		PauseForReload(false);
	}
}

void FVoxelLandGraphToolkit::ObjectsReinstanced(const TMap<UObject*, UObject*>&)
{
	if (!bPackageReloadInProgress)
	{
		ResumeAfterReload();
	}
}

void FVoxelLandGraphToolkit::M4ObjectTransacted(UObject* Object, const FTransactionObjectEvent& Event)
{
	if (bStopped || !bM4Preview || !M4PreviewConfig || !Object
		|| Event.GetEventType() != ETransactionObjectEventType::UndoRedo)
	{
		return;
	}
	if (Object == M4PreviewConfig)
	{
		if (Event.HasNonPropertyChanges() || !Event.HasPropertyChanges())
		{
			bM4UndoSemanticChange = true;
			return;
		}
		for (const FName Property : Event.GetChangedProperties())
		{
			if (Property == GET_MEMBER_NAME_CHECKED(UVoxelLandM4WorldConfig, MaterialSetup))
			{
				bM4UndoMaterialChange = true;
			}
			else
			{
				bM4UndoSemanticChange = true;
			}
		}
		return;
	}
	if (Object == M4PreviewConfig->MaterialSetup.Get())
	{
		bM4UndoMaterialChange = true;
		return;
	}
	if (Object == M4PreviewConfig->Continental.Graph || Object == M4PreviewConfig->Mountain.Graph
		|| Object == M4PreviewConfig->Peaks.Graph
		|| M4FunctionDependencies.ContainsByPredicate(
			[Object](const TWeakObjectPtr<UObject>& Item) { return Item.Get() == Object; }))
	{
		// EditorGraph is layout-only; other changes or unknown transaction
		// metadata conservatively invalidate the control program.
		const TArray<FName>& Properties = Event.GetChangedProperties();
		const bool bOnlyLayout = !Event.HasNonPropertyChanges() && !Properties.IsEmpty()
							  && Properties.ContainsByPredicate([](FName Name) {
									 return Name == GET_MEMBER_NAME_CHECKED(UVoxelLandTerrainGraph, EditorGraph);
								 })
							  && Properties.Num() == 1;
		bM4UndoSemanticChange |= !bOnlyLayout;
	}
}

void FVoxelLandGraphToolkit::M4ControlGraphSourceChanged(UVoxelLandTerrainGraph* Changed)
{
	// The originating toolkit already schedules compilation/preview through
	// its own mirror. Do not create a reentrant notification cycle.
	if (bStopped || bPIE || bReloadPaused || !bM4Preview || !M4PreviewConfig || !Changed || Changed == Asset)
	{
		return;
	}
	if (Changed == M4PreviewConfig->Continental.Graph || Changed == M4PreviewConfig->Mountain.Graph
		|| Changed == M4PreviewConfig->Peaks.Graph)
	{
		StartM4Preview();
	}
}

void FVoxelLandGraphToolkit::MaterialSetupPropertyChanged(UObject* Object, FPropertyChangedEvent& Event)
{
	if (!IsInGameThread())
	{
		return;
	}
	if (!bStopped && !GIsTransacting && bM4Preview && M4PreviewConfig)
	{
		if ((Object == M4PreviewConfig
				&& Event.GetPropertyName() == GET_MEMBER_NAME_CHECKED(UVoxelLandM4WorldConfig, MaterialSetup))
			|| (Object && Object == M4PreviewConfig->MaterialSetup.Get()))
		{
			const UVoxelLandMaterialSetup* Setup = M4PreviewConfig->MaterialSetup.LoadSynchronous();
			ApplyPreviewMaterial(Setup && Setup->FormatVersion == 1 ? Setup->Material.LoadSynchronous() : nullptr);
			return;
		}
		const bool bM4FunctionAffected =
			Object && M4FunctionDependencies.ContainsByPredicate([Object](const TWeakObjectPtr<UObject>& Dependency) {
				return Dependency.Get() == Object;
			});
		if (bM4FunctionAffected || Object == M4PreviewConfig || Object == M4PreviewConfig->Continental.Graph
			|| Object == M4PreviewConfig->Mountain.Graph || Object == M4PreviewConfig->Peaks.Graph)
		{
			StartM4Preview();
			return;
		}
	}
	if (!bStopped && !GIsTransacting && PreviewSettings && PreviewSettings->MaterialSetup.Get() == Object)
	{
		SynchronizePreviewSettings(true);
	}
}

void FVoxelLandGraphToolkit::ApplyPreviewMaterial(UMaterialInterface* Material)
{
	// A material change alters the inspected image without regenerating terrain.
	M4InspectionDetails.Reset();
	SelectedPreviewMaterial = Material;
#if WITH_DEV_AUTOMATION_TESTS
	++MaterialApplyCountForTests;
#endif
	if (!Material || !Material->GetMaterial() || Material->GetMaterial()->MaterialDomain != MD_Surface)
	{
		PreviewMaterial = UMaterial::GetDefaultMaterial(MD_Surface);
		MaterialDiagnostic = TEXT("Missing or unsupported material; using the default surface material.");
	}
	else
	{
		PreviewMaterial = Material;
		MaterialDiagnostic.Reset();
	}
	for (UVoxelLandChunkComponent* Component : PreviewComponents)
	{
		if (Component)
		{
			Component->SetMaterial(0, PreviewMaterial);
		}
	}
}

void FVoxelLandGraphToolkit::ApplyMaterialSetup()
{
	const UVoxelLandMaterialSetup* Setup = PreviewSettings ? PreviewSettings->MaterialSetup.LoadSynchronous() : nullptr;
	ApplyPreviewMaterial(Setup && Setup->FormatVersion == 1 ? Setup->Material.LoadSynchronous() : nullptr);
}

void FVoxelLandGraphToolkit::MaterialChanged(const FAssetData& Material)
{
	ApplyPreviewMaterial(Cast<UMaterialInterface>(Material.GetAsset()));
}

FString FVoxelLandGraphToolkit::MaterialPickerPath() const
{
	return SelectedPreviewMaterial.ToSoftObjectPath().ToString();
}

FString FVoxelLandGraphToolkit::SettingsPickerPath() const
{
	return SelectedPreviewSettings.ToSoftObjectPath().ToString();
}

FString FVoxelLandGraphToolkit::M4PreviewConfigPath() const
{
	return M4PreviewConfig ? M4PreviewConfig->GetPathName() : FString();
}

void FVoxelLandGraphToolkit::M4PreviewConfigChanged(const FAssetData& Settings)
{
	UVoxelLandM4WorldConfig* Source = Cast<UVoxelLandM4WorldConfig>(Settings.GetAsset());
	if (Settings.IsValid() && !Source)
	{
		return;
	}
	M4PreviewConfig = Source;
	if (bM4Preview)
	{
		StartM4Preview();
	}
}

void FVoxelLandGraphToolkit::SettingsChanged(const FAssetData& Settings)
{
	UVoxelLandWorldSettings* Source = Cast<UVoxelLandWorldSettings>(Settings.GetAsset());
	if (Settings.IsValid() && !Source)
	{
		return;
	}
	SelectedPreviewSettings = Source;
	if (Source)
	{
		PreviewSettings = DuplicateObject<UVoxelLandWorldSettings>(Source, GetTransientPackage());
		PreviewSettings->SetFlags(RF_Transient | RF_Transactional);
	}
	else
	{
		PreviewSettings = NewObject<UVoxelLandWorldSettings>(GetTransientPackage(), NAME_None,
			RF_Transient | RF_Transactional);
		PreviewSettings->Graph = Asset;
	}
	if (PreviewDetails)
	{
		PreviewDetails->SetObject(PreviewSettings, true);
	}
	SynchronizePreviewSettings(true);
	// Picking settings is an explicit material selection even if its setup path
	// matches the prior one (the user may have selected a direct material).
	ApplyMaterialSetup();
}

void FVoxelLandGraphToolkit::PreviewPropertiesChanged(const FPropertyChangedEvent&)
{
	SynchronizePreviewSettings(true);
}

void FVoxelLandGraphToolkit::SynchronizePreviewSettings(bool bApplyChanges)
{
	if (!PreviewSettings)
	{
		return;
	}
	FCanonicalWriter Writer;
	Writer.String(GetPathNameSafe(PreviewSettings->Graph));
	Writer.U32(uint32(PreviewSettings->FormatVersion));
	Writer.U32(uint32(uint64(PreviewSettings->Seed)));
	Writer.U32(uint32(uint64(PreviewSettings->Seed) >> 32));
	Writer.Float(PreviewSettings->VoxelSizeCm);
	Writer.U32(uint32(PreviewSettings->HorizontalRadius));
	Writer.U32(uint32(PreviewSettings->VerticalRadius));
	TArray<FVoxelLandParameterOverride> Sorted = PreviewSettings->Overrides;
	Sorted.Sort([](const FVoxelLandParameterOverride& A, const FVoxelLandParameterOverride& B) {
		if (A.ParameterId.A != B.ParameterId.A) { return A.ParameterId.A < B.ParameterId.A; }
		if (A.ParameterId.B != B.ParameterId.B) { return A.ParameterId.B < B.ParameterId.B; }
		if (A.ParameterId.C != B.ParameterId.C) { return A.ParameterId.C < B.ParameterId.C; }
		return A.ParameterId.D < B.ParameterId.D;
	});
	Writer.U32(Sorted.Num());
	for (const FVoxelLandParameterOverride& O : Sorted)
	{
		Writer.U32(O.ParameterId.A);
		Writer.U32(O.ParameterId.B);
		Writer.U32(O.ParameterId.C);
		Writer.U32(O.ParameterId.D);
		Writer.U8(uint8(O.Type));
		if (O.Type == EVoxelLandGraphValueType::Float) { Writer.Float(O.FloatValue); }
		else if (O.Type == EVoxelLandGraphValueType::Int32) { Writer.U32(uint32(O.IntValue)); }
		else { Writer.U8(O.BoolValue ? 1 : 0); }
	}
	FCanonicalWriter MaterialWriter;
	MaterialWriter.String(PreviewSettings->MaterialSetup.ToSoftObjectPath().ToString());
	const UVoxelLandMaterialSetup* Setup = PreviewSettings->MaterialSetup.LoadSynchronous();
	MaterialWriter.U8(Setup ? 1 : 0);
	if (Setup)
	{
		MaterialWriter.U32(uint32(Setup->FormatVersion));
		MaterialWriter.String(Setup->Material.ToSoftObjectPath().ToString());
	}
	const bool bDensityChanged = bHasPreviewSettingsSnapshot && PreviewDensitySnapshot != Writer.Bytes;
	const bool bMaterialChanged = bHasPreviewSettingsSnapshot && PreviewMaterialSnapshot != MaterialWriter.Bytes;
	PreviewDensitySnapshot = MoveTemp(Writer.Bytes);
	PreviewMaterialSnapshot = MoveTemp(MaterialWriter.Bytes);
	bHasPreviewSettingsSnapshot = true;
	PreviewSeed = PreviewSettings->Seed;
	PreviewHorizontalRadius = FMath::Clamp(PreviewSettings->HorizontalRadius, 1, 2);
	PreviewVerticalRadius = FMath::Clamp(PreviewSettings->VerticalRadius, 1, 2);
	if (bApplyChanges && bMaterialChanged)
	{
		ApplyMaterialSetup();
	}
	if (bApplyChanges && bDensityChanged)
	{
		SemanticChangeTime = FPlatformTime::Seconds();
		StartPreview();
	}
}

FReply FVoxelLandGraphToolkit::ResetPreviewCamera()
{
	if (PreviewViewport)
	{
		PreviewViewport->ResetCamera();
	}
	return FReply::Handled();
}

FReply FVoxelLandGraphToolkit::FramePreviewTerrain()
{
	if (!bStopped && PreviewViewport)
	{
		PreviewViewport->FrameTerrain();
	}
	return FReply::Handled();
}

bool FVoxelLandGraphToolkit::BuildPreviewProfile(const FVoxelLandSerializedProgram& Payload,
	FVoxelLandZoneProfile& Profile, FString& Error) const
{
	if (!PreviewSettings || PreviewSettings->Graph != Asset || !PreviewSettings->ValidateSettings(Error))
	{
		Error = FString::Printf(TEXT("Settings must reference this graph and be valid. %s"), *Error);
		return false;
	}
	Profile.VoxelSizeCm = PreviewSettings->VoxelSizeCm;
	for (const FVoxelLandParameterOverride& Override : PreviewSettings->Overrides)
	{
		const FVoxelLandSerializedParameter* Parameter = Payload.Parameters.FindByPredicate(
			[&Override](const FVoxelLandSerializedParameter& Candidate) {
				return Candidate.Id == Override.ParameterId;
			});
		if (!Parameter)
		{
			// Unreachable declarations are valid settings but have no VM slot.
			continue;
		}
		if (Override.Type == EVoxelLandGraphValueType::Float)
		{
			Profile.FloatOverrides.Add(Parameter->Name, Override.FloatValue);
		}
		else if (Override.Type == EVoxelLandGraphValueType::Int32)
		{
			Profile.IntOverrides.Add(Parameter->Name, Override.IntValue);
		}
		else
		{
			Profile.BoolOverrides.Add(Parameter->Name, Override.BoolValue);
		}
	}
	return true;
}

namespace
{
template <typename T> bool M4PreviewArraysEqual(const TArray<T>& A, const TArray<T>& B)
{
	if (A.Num() != B.Num())
	{
		return false;
	}
	for (int32 Index = 0; Index < A.Num(); ++Index)
	{
		if (!T::StaticStruct()->CompareScriptStruct(&A[Index], &B[Index], 0))
		{
			return false;
		}
	}
	return true;
}

bool M4PreviewSourceEqual(const FVoxelLandGraphSnapshot& A, const FVoxelLandGraphSnapshot& B)
{
	if (A.SourceVersion != B.SourceVersion || !M4PreviewArraysEqual(A.Parameters, B.Parameters)
		|| !M4PreviewArraysEqual(A.Nodes, B.Nodes) || !M4PreviewArraysEqual(A.Links, B.Links)
		|| A.Functions.Num() != B.Functions.Num())
	{
		return false;
	}
	for (int32 Index = 0; Index < A.Functions.Num(); ++Index)
	{
		const FVoxelLandGraphFunctionSnapshot& Left = A.Functions[Index];
		const FVoxelLandGraphFunctionSnapshot& Right = B.Functions[Index];
		if (Left.AssetPath != Right.AssetPath || Left.SourceVersion != Right.SourceVersion
			|| Left.OutputType != Right.OutputType || Left.OutputNode != Right.OutputNode
			|| Left.OutputPin != Right.OutputPin || !M4PreviewArraysEqual(Left.Inputs, Right.Inputs)
			|| !M4PreviewArraysEqual(Left.Nodes, Right.Nodes) || !M4PreviewArraysEqual(Left.Links, Right.Links))
		{
			return false;
		}
	}
	return true;
}

void M4InspectionAppendOverrides(FString& Out, const TCHAR* Label, const FVoxelLandM4ControlSnapshot& Control)
{
	Out += FString::Printf(TEXT("%s source/program/LUT hashes: %lld / %lld / %lld (LUT v%d)\n"), Label,
		Control.Payload.SourceFingerprint, Control.Payload.ProgramFingerprint, Control.CookedCurve.SamplesHash,
		Control.CookedCurve.BakeVersion);
	Out += FString::Printf(TEXT("%s overrides:"), Label);
	if (Control.Overrides.IsEmpty())
	{
		Out += TEXT(" defaults");
	}
	for (const FVoxelLandParameterOverride& Override : Control.Overrides)
	{
		const FString Id = Override.ParameterId.ToString();
		switch (Override.Type)
		{
			case EVoxelLandGraphValueType::Float:
				Out += FString::Printf(TEXT(" %s=%.9g"), *Id, double(Override.FloatValue));
				break;
			case EVoxelLandGraphValueType::Int32:
				Out += FString::Printf(TEXT(" %s=%d"), *Id, Override.IntValue);
				break;
			case EVoxelLandGraphValueType::Bool:
				Out += FString::Printf(TEXT(" %s=%s"), *Id, Override.BoolValue ? TEXT("true") : TEXT("false"));
				break;
			default:
				Out += FString::Printf(TEXT(" %s=<unsupported>"), *Id);
				break;
		}
	}
	Out += TEXT("\n");
}
}

bool FVoxelLandGraphToolkit::IsM4PreviewCurrent(const FVoxelLandPreviewWork& Work) const
{
	UVoxelLandM4WorldConfig* Config = Work.M4Config.Get();
	if (!Config || Config != M4PreviewConfig || !Work.M4Snapshot || !Config->MatchesSnapshot(*Work.M4Snapshot)
		|| Config->Continental.Graph != Work.M4Continental.Get() || Config->Mountain.Graph != Work.M4Mountain.Get()
		|| Config->Peaks.Graph != Work.M4Peaks.Get())
	{
		return false;
	}
	FString Error;
	FVoxelLandM4WorldSnapshot Current;
	if (!Config->ValidateBakedCurves(Error) || !Config->CaptureSnapshot(Current, Error))
	{
		return false;
	}
	const FVoxelLandM4ControlSnapshot* A[] = {
		&Work.M4Snapshot->Continental, &Work.M4Snapshot->Mountain, &Work.M4Snapshot->Peaks};
	const FVoxelLandM4ControlSnapshot* B[] = {&Current.Continental, &Current.Mountain, &Current.Peaks};
	for (int32 Index = 0; Index < 3; ++Index)
	{
		if (!M4PreviewSourceEqual(A[Index]->Source, B[Index]->Source)
			|| !FVoxelLandSerializedProgram::StaticStruct()->CompareScriptStruct(
				&A[Index]->Payload, &B[Index]->Payload, 0)
			|| !FVoxelLandM4CookedCurve::StaticStruct()->CompareScriptStruct(
				&A[Index]->CookedCurve, &B[Index]->CookedCurve, 0))
		{
			return false;
		}
	}
	return true;
}

void FVoxelLandGraphToolkit::StartM4Preview()
{
	if (bStopped || bPIE || bReloadPaused)
	{
		return;
	}
	CancelPreview();
	M4StageDiagnostics = TEXT("M4 stages: generating; earlier inspector values are stale.");
	M4InspectionDetails.Reset();
	bNoisePreview = false;
	if (PreviewWork)
	{
		bPreviewRequested = true;
		Status = TEXT("Generating M4 terrain; previous preview is stale.");
		return;
	}
	if (!M4PreviewConfig)
	{
		Status = TEXT("Stale preview: select an M4 world configuration.");
		return;
	}
	TSharedPtr<const FVoxelLandM4GridLease, ESPMode::ThreadSafe> Lease;
	if (M4PreviewConfig->bUnlimited)
	{
		Lease = VoxelLandM4Preparation::Reserve();
	}
	else if (!VoxelLandM4Preparation::EnsureCoordinatorReady())
	{
		Status = TEXT("Stale preview: M4 shared prepass coordinator is unavailable.");
		return;
	}
	if (M4PreviewConfig->bUnlimited && !Lease)
	{
		Status = TEXT("Stale preview: M4 prepass admission budget is exhausted.");
		return;
	}
	const TSharedPtr<const FVoxelLandM4GridLease, ESPMode::ThreadSafe> ValidationLease =
		VoxelLandM4Preparation::ReserveValidation();
	if (!ValidationLease)
	{
		Status = TEXT("Stale preview: M4 snapshot validation capacity is exhausted.");
		return;
	}
	FString Error;
	auto Snapshot = MakeShared<FVoxelLandM4WorldSnapshot, ESPMode::ThreadSafe>();
	if (!M4PreviewConfig->ValidateBakedCurves(Error) || !M4PreviewConfig->CaptureSnapshot(*Snapshot, Error))
	{
		Status = FString::Printf(TEXT("Error; previous preview is stale: %s"), *Error);
		return;
	}
	TArray<TWeakObjectPtr<UObject>> Dependencies;
	const FVoxelLandM4ControlSnapshot* Controls[] = {&Snapshot->Continental, &Snapshot->Mountain, &Snapshot->Peaks};
	for (const FVoxelLandM4ControlSnapshot* Control : Controls)
	{
		for (const FVoxelLandGraphFunctionSnapshot& Function : Control->Source.Functions)
		{
			UObject* Dependency = FSoftObjectPath(Function.AssetPath).ResolveObject();
			if (!Dependency)
			{
				Status = TEXT("Stale preview: a graph function dependency is unavailable.");
				return;
			}
			Dependencies.AddUnique(Dependency);
		}
	}
	M4FunctionDependencies = MoveTemp(Dependencies);
	M4FunctionPresentationRevision = FVoxelLandEditorOperatorRegistry::GetRevision();
	if (bM4Inspection && Snapshot->Domain.Mode != EVoxelLandM4DomainMode::Finite)
	{
		Status = TEXT("2 km inspection requires a finite prepass height grid; select finite mode.");
		return;
	}
	// The inspection profile is a distinct descriptor. Preserve the authored
	// snapshot separately for the freshness check; do not modify the asset.
	auto ExecutionSnapshot = MakeShared<FVoxelLandM4WorldSnapshot, ESPMode::ThreadSafe>(*Snapshot);
	if (bM4Inspection)
	{
		ExecutionSnapshot->VoxelSizeCm = VoxelLandM4Inspection::VoxelSizeCm;
	}
	const int32 Horizontal = FMath::Min(Snapshot->HorizontalRadius, 2);
	const int32 Vertical = FMath::Min(Snapshot->VerticalRadius, 2);
	const TSharedRef<TAtomic<bool>, ESPMode::ThreadSafe> Cancel = MakeShared<TAtomic<bool>, ESPMode::ThreadSafe>(false);
	PreviewCancel = Cancel;
	const TSharedRef<FVoxelLandPreviewWork, ESPMode::ThreadSafe> Work =
		MakeShared<FVoxelLandPreviewWork, ESPMode::ThreadSafe>();
	Work->bM4 = true;
	Work->bM4Inspection = bM4Inspection;
	Work->M4ValidationLease = ValidationLease;
	Work->M4Snapshot = Snapshot;
	Work->M4ExecutionSnapshot = ExecutionSnapshot;
	Work->M4Config = M4PreviewConfig;
	Work->M4Continental = M4PreviewConfig->Continental.Graph;
	Work->M4Mountain = M4PreviewConfig->Mountain.Graph;
	Work->M4Peaks = M4PreviewConfig->Peaks.Graph;
	Work->PreviewRevision = PreviewRevision;
	Work->SourceRevision = Revision;
	Work->VoxelSize = float(ExecutionSnapshot->VoxelSizeCm);
	Work->ChunkCount = (2 * Horizontal + 1) * (2 * Horizontal + 1) * 2 * Vertical;
#if WITH_DEV_AUTOMATION_TESTS
	Work->SeedForTests = Snapshot->Seed;
	++PreviewStartCountForTests;
#endif
	PreviewWork = Work;
	SemanticChangeTime = FPlatformTime::Seconds();
	Status = TEXT("Generating M4 terrain; previous preview is stale.");
	WorkerTasks.Add(Async(EAsyncExecution::ThreadPool, [Work, Lease, Cancel, Horizontal, Vertical]() {
		const double Started = FPlatformTime::Seconds();
		TSharedPtr<const FVoxelLandGenerator, ESPMode::ThreadSafe> Generator;
		auto IsCancelled = [&Cancel] {
			return Cancel->Load();
		};
		if (VoxelLandM4Preparation::Prepare(
				*Work->M4ExecutionSnapshot, Lease, Work->M4ValidationLease, IsCancelled, Generator, Work->Error))
		{
			const TSharedPtr<const FVoxelLandM4HeightGrid, ESPMode::ThreadSafe> Grid = Generator->GetM4HeightGrid();
			if (Grid)
			{
				Grid->GetHeightRangeQ8(Work->M4MinHeightQ8, Work->M4MaxHeightQ8);
				Grid->GetBoundsCm(Work->M4MinX, Work->M4MinY, Work->M4MaxX, Work->M4MaxY);
				Work->bHasM4StageStats = Grid->GetStageStats(Work->M4StageStats);
				Work->bHasM4HeightRange = true;
				if (!VoxelLandM4StageMap::Build(*Grid, IsCancelled, Work->M4StagePixels))
				{
					Work->Error = TEXT("M4 stage map sampling failed or was cancelled");
					Work->GenerationMs = (FPlatformTime::Seconds() - Started) * 1000.0;
					Work->Done.Store(true);
					return;
				}
			}
			if (Work->bM4Inspection)
			{
				const int32 BandQ8 =
					Work->M4ExecutionSnapshot->bOverhangEnabled && Work->M4ExecutionSnapshot->OverhangAmplitudeQ8 > 0
						? Work->M4ExecutionSnapshot->OverhangBandQ8
						: 0;
				const auto& Detail = Work->M4ExecutionSnapshot->HeightDetail;
				const int32 DetailAmplitudeQ8 = Detail.bEnabled ? Detail.AmplitudeQ8 : 0;
				if (!Grid
					|| !VoxelLandM4Inspection::VerticalChunkRange(Work->M4MinHeightQ8, Work->M4MaxHeightQ8, BandQ8,
						DetailAmplitudeQ8, Work->M4MinChunkZ, Work->M4MaxChunkZ))
				{
					Work->Error = TEXT(
						"2 km inspection exceeds the 16-chunk vertical cap or supported Z range, including clearance.");
				}
				else
				{
					Work->ChunkCount = 9 * (Work->M4MaxChunkZ - Work->M4MinChunkZ + 1);
					Work->bSuccess = FVoxelLandPreviewJob::RunPreparedRange(Generator, 1, Work->M4MinChunkZ,
						Work->M4MaxChunkZ, Cancel, Work->Meshes, Work->Error, VoxelLandM4Inspection::MaxMeshBytes);
					if (Work->bSuccess && Work->Meshes.IsEmpty())
					{
						Work->bSuccess = false;
						Work->Error =
							TEXT("2 km inspection generated no visible surface; check terrain and domain bounds.");
					}
				}
			}
			else
			{
				Work->bSuccess = FVoxelLandPreviewJob::RunPrepared(
					Generator, Horizontal, Vertical, Cancel, Work->Meshes, Work->Error);
			}
		}
		Work->GenerationMs = (FPlatformTime::Seconds() - Started) * 1000.0;
		Work->Done.Store(true);
	}));
}

void FVoxelLandGraphToolkit::StartPreview()
{
	if (bM4Preview)
	{
		StartM4Preview();
		return;
	}
	if (bStopped || bPIE || bReloadPaused)
	{
		return;
	}
	CancelPreview();
	if (bNoisePreview && (!bNoiseSpacingValid || !bNoiseSliceValid))
	{
		Status = TEXT("Error; preview stale: enter valid 2D noise spacing and world Z values.");
		return;
	}
	if (SemanticChangeTime <= 0.0)
	{
		SemanticChangeTime = FPlatformTime::Seconds();
	}
	if (PreviewWork)
	{
		bPreviewRequested = true;
		Status = TEXT("Generating; previous preview is stale.");
		return;
	}
	FString Error;
	FVoxelLandGraphSnapshot Snapshot;
	bool bValidDefaults = true;
	const bool bSnapshotValid = PreviewNodeId.IsValid()
		                            ? CaptureMirrorWithDependencies(Snapshot, bValidDefaults, Error) && bValidDefaults
		                            : Asset->CaptureSnapshot(Snapshot, Error);
	if (!bSnapshotValid)
	{
		Status = FString::Printf(TEXT("Stale preview: source unavailable or invalid. %s"), *Error);
		return;
	}
	const uint64 SourceHash = FVoxelLandProgramCodec::SourceFingerprint(Snapshot);
	FVoxelLandSerializedProgram Payload;
	if (PreviewNodeId.IsValid())
	{
		if (!NodePreviewPayload || !NodePreviewSnapshot || NodePreviewSourceHash != SourceHash)
		{
			Status = TEXT("Stale node preview: compile the selected output first.");
			return;
		}
		Payload = *NodePreviewPayload;
		Snapshot = *NodePreviewSnapshot;
	}
	else
	{
		Payload = Asset->CompiledProgram;
	}
	if (Payload.ProgramVersion == 0 || uint64(Payload.SourceFingerprint) !=
	    FVoxelLandProgramCodec::SourceFingerprint(Snapshot))
	{
		Status = TEXT("Stale preview: compiled source unavailable or invalid.");
		return;
	}
	FVoxelLandZoneProfile Profile;
	if (!BuildPreviewProfile(Payload, Profile, Error))
	{
		Status = FString::Printf(TEXT("Error; preview is stale: %s"), *Error);
		return;
	}
	const int64 Seed = PreviewSeed;
	const TSharedRef<TAtomic<bool>, ESPMode::ThreadSafe> Cancel = MakeShared<TAtomic<bool>, ESPMode::ThreadSafe>(false);
	PreviewCancel = Cancel;
	const int32 HorizontalRadius = PreviewHorizontalRadius;
	const int32 VerticalRadius = PreviewVerticalRadius;
	const bool bMakeNoise2D = bNoisePreview;
	const float SpacingCm = NoiseSpacingCm;
	const float SliceZCm = NoiseSliceZCm;
	Status = bMakeNoise2D
		         ? TEXT("Generating 2D noise; previous preview is stale.")
		         : TEXT("Generating bounded chunks; previous preview is stale.");
	const TSharedRef<FVoxelLandPreviewWork, ESPMode::ThreadSafe> Work = MakeShared<FVoxelLandPreviewWork,
		ESPMode::ThreadSafe>();
	Work->PreviewRevision = PreviewRevision;
	Work->SourceRevision = Revision;
	Work->bNoise2D = bMakeNoise2D;
#if WITH_DEV_AUTOMATION_TESTS
	Work->SeedForTests = Seed;
#endif
	Work->VoxelSize = Profile.VoxelSizeCm;
	Work->ChunkCount = (2 * HorizontalRadius + 1) * (2 * HorizontalRadius + 1) * 2 * VerticalRadius;
	PreviewWork = Work;
#if WITH_DEV_AUTOMATION_TESTS
	++PreviewStartCountForTests;
#endif
#if WITH_DEV_AUTOMATION_TESTS
	const TSharedPtr<FVoxelLandReloadTestBarrier, ESPMode::ThreadSafe> Barrier = MoveTemp(PreviewReloadBarrierForTests);
	WorkerTasks.Add(Async(EAsyncExecution::ThreadPool,
		[Snapshot = MoveTemp(Snapshot), Payload, Profile = MoveTemp(Profile), Seed, HorizontalRadius, VerticalRadius,
			SpacingCm, SliceZCm, bMakeNoise2D, Cancel, Work, Barrier]() mutable
#else
		WorkerTasks.Add(Async(EAsyncExecution::ThreadPool,
			[Snapshot = MoveTemp(Snapshot), Payload, Profile = MoveTemp(Profile), Seed, HorizontalRadius, VerticalRadius
				, SpacingCm, SliceZCm, bMakeNoise2D, Cancel, Work]() mutable
#endif
		{
			const double StartTime = FPlatformTime::Seconds();
			if (bMakeNoise2D)
			{
				Work->bSuccess = FVoxelLandNoisePreviewJob::Run(Snapshot, Payload, Seed, Profile, SpacingCm, SliceZCm,
					Cancel, Work->NoisePixels, Work->NoiseMin, Work->NoiseMax, Work->NoiseClipped, Work->Error);
			}
			else
			{
				Work->bSuccess = FVoxelLandPreviewJob::Run(Snapshot, Payload, Seed, Profile, HorizontalRadius,
					VerticalRadius, Cancel, Work->Meshes, Work->Error
#if WITH_DEV_AUTOMATION_TESTS
					, Barrier
#endif
					);
			}
			Work->GenerationMs = (FPlatformTime::Seconds() - StartTime) * 1000.0;
			Work->Done.Store(true);
		}));

}

bool FVoxelLandGraphToolkit::PublishNoisePreview(const FVoxelLandPreviewWork& Finished)
{
	constexpr int32 Resolution = FVoxelLandNoisePreviewJob::Resolution;
	if (Finished.NoisePixels.Num() != Resolution * Resolution * 4)
	{
		Status = TEXT("Error; previous preview is stale: invalid 2D image data.");
		return false;
	}
	UTexture2D* Texture = NoiseTexture.Get();
	if (!Texture)
	{
		Texture = UTexture2D::CreateTransient(Resolution, Resolution, PF_B8G8R8A8);
	}
	if (!Texture || !Texture->GetPlatformData() || Texture->GetPlatformData()->Mips.IsEmpty())
	{
		Status = TEXT("Error; previous preview is stale: could not allocate the 2D image.");
		return false;
	}
	void* Destination = Texture->GetPlatformData()->Mips[0].BulkData.Lock(LOCK_READ_WRITE);
	if (!Destination)
	{
		Texture->GetPlatformData()->Mips[0].BulkData.Unlock();
		Status = TEXT("Error; previous preview is stale: could not update the 2D image.");
		return false;
	}
	FMemory::Memcpy(Destination, Finished.NoisePixels.GetData(), Finished.NoisePixels.Num());
	Texture->GetPlatformData()->Mips[0].BulkData.Unlock();
	Texture->SRGB = true;
	Texture->NeverStream = true;
	Texture->Filter = TF_Nearest;
	Texture->UpdateResource();
	NoiseTexture = Texture;
	NoiseBrush.SetResourceObject(Texture);
	NoiseBrush.SetImageSize(FVector2D(Resolution, Resolution));
	Status = FString::Printf(
		TEXT("Ready: observed range %.6g to %.6g; %d of %d samples clipped outside [-1, 1]; CPU %.1f ms."),
		Finished.NoiseMin, Finished.NoiseMax, Finished.NoiseClipped, Resolution * Resolution, Finished.GenerationMs);
	SemanticChangeTime = 0.0;
	return true;
}

const FSlateBrush* FVoxelLandGraphToolkit::GetM4StageBrush() const
{
	return &M4StageBrush;
}

const FSlateBrush* FVoxelLandGraphToolkit::GetM4ControlsBrush() const
{
	return &M4ControlsBrush;
}

bool FVoxelLandGraphToolkit::PublishM4ControlMaps(const FVoxelLandM4StageStats& Stats)
{
	if (!Stats.bHasControlMaps)
	{
		return false;
	}
	constexpr int32 R = FVoxelLandM4StageStats::ControlMapResolution;
	constexpr int32 Width = 3 * R;
	UTexture2D* Texture = M4ControlsTexture.Get();
	if (!Texture)
	{
		Texture = UTexture2D::CreateTransient(Width, R, PF_B8G8R8A8);
	}
	if (!Texture || !Texture->GetPlatformData() || Texture->GetPlatformData()->Mips.IsEmpty())
	{
		return false;
	}
	uint8* Destination = static_cast<uint8*>(Texture->GetPlatformData()->Mips[0].BulkData.Lock(LOCK_READ_WRITE));
	if (!Destination)
	{
		Texture->GetPlatformData()->Mips[0].BulkData.Unlock();
		return false;
	}
	for (int32 Y = 0; Y < R; ++Y)
	{
		for (int32 Channel = 0; Channel < 3; ++Channel)
		{
			for (int32 X = 0; X < R; ++X)
			{
				const uint8 Gray = Stats.ControlMaps[Channel][Y * R + X];
				const int32 Offset = (Y * Width + Channel * R + X) * 4;
				Destination[Offset] = Destination[Offset + 1] = Destination[Offset + 2] = Gray;
				Destination[Offset + 3] = 255;
			}
		}
	}
	Texture->GetPlatformData()->Mips[0].BulkData.Unlock();
	Texture->SRGB = true;
	Texture->NeverStream = true;
	Texture->Filter = TF_Nearest;
	Texture->UpdateResource();
	M4ControlsTexture = Texture;
	M4ControlsBrush.SetResourceObject(Texture);
	M4ControlsBrush.SetImageSize(FVector2D(Width, R));
	return true;
}

bool FVoxelLandGraphToolkit::PublishM4StageMap(const TArray<uint8>& Pixels)
{
	constexpr int32 Resolution = VoxelLandM4StageMap::Resolution;
	if (Pixels.Num() != Resolution * Resolution * 4)
	{
		return false;
	}
	UTexture2D* Texture = M4StageTexture.Get();
	if (!Texture)
	{
		Texture = UTexture2D::CreateTransient(Resolution, Resolution, PF_B8G8R8A8);
	}
	if (!Texture || !Texture->GetPlatformData() || Texture->GetPlatformData()->Mips.IsEmpty())
	{
		return false;
	}
	void* Destination = Texture->GetPlatformData()->Mips[0].BulkData.Lock(LOCK_READ_WRITE);
	if (!Destination)
	{
		Texture->GetPlatformData()->Mips[0].BulkData.Unlock();
		return false;
	}
	FMemory::Memcpy(Destination, Pixels.GetData(), Pixels.Num());
	Texture->GetPlatformData()->Mips[0].BulkData.Unlock();
	Texture->SRGB = true;
	Texture->NeverStream = true;
	Texture->Filter = TF_Nearest;
	Texture->UpdateResource();
	M4StageTexture = Texture;
	M4StageBrush.SetResourceObject(Texture);
	M4StageBrush.SetImageSize(FVector2D(Resolution, Resolution));
	return true;
}

bool FVoxelLandGraphToolkit::PublishPreview(const FVoxelLandPreviewWork& Finished)
{
	const double UploadStart = FPlatformTime::Seconds();
	TArray<TPair<FIntVector, UVoxelLandChunkComponent*>> Staged;
	for (const auto& Entry : Finished.Meshes)
	{
		UVoxelLandChunkComponent* Component = NewObject<UVoxelLandChunkComponent>(GetTransientPackage(), NAME_None,
			RF_Transient);
		Component->SetMaterial(0, PreviewMaterial);
		if (!Component->SetMesh(Entry.Value))
		{
			Status = TEXT("Error; previous preview is stale: a preview mesh was rejected.");
			return false;
		}
		Staged.Emplace(Entry.Key, Component);
	}
	ClearPreview();
	const bool bGPURequested = CVarVoxelLandPreviewGPUUploadTiming.GetValueOnGameThread() != 0;
	TSharedPtr<FVoxelLandPreviewUploadObservation, ESPMode::ThreadSafe> UploadObservation;
	if (bGPURequested && !Staged.IsEmpty() && FVoxelLandPreviewUploadObservation::IsSupported())
	{
		UploadObservation = MakeShared<FVoxelLandPreviewUploadObservation, ESPMode::ThreadSafe>(Staged.Num());
	}
	FBox TerrainBounds(ForceInit);
	for (const auto& Entry : Staged)
	{
		Entry.Value->SetPreviewUploadObservation(UploadObservation);
		PreviewScene->AddComponent(Entry.Value,
			FTransform(VoxelLandMeshCoordinates::RenderOrigin(Entry.Key, Finished.VoxelSize)));
		PreviewComponents.Add(Entry.Value);
		TerrainBounds += Entry.Value->Bounds.GetBox();
	}
	if (PreviewViewport)
	{
		PreviewViewport->SetTerrainBounds(TerrainBounds);
	}
	if (Finished.bM4 && Finished.M4Snapshot)
	{
		const FVoxelLandM4WorldSnapshot& Snapshot = *Finished.M4Snapshot;
		if (Finished.bHasM4HeightRange)
		{
			const bool bMapPublished = PublishM4StageMap(Finished.M4StagePixels);
			if (!bMapPublished)
			{
				M4StageBrush.SetResourceObject(nullptr);
				M4StageTexture = nullptr;
			}
			M4StageDiagnostics = FString::Printf(
				TEXT(
					"M4 global-stage inspector (completed CPU prepass): grid XY [%lld,%lld] to [%lld,%lld] cm, %d x %d cells, spacing %lld cm; stored height range %.2f to %.2f cm. Erosion %s. Range excludes overhang and final smoothing."),
				Finished.M4MinX, Finished.M4MinY, Finished.M4MaxX, Finished.M4MaxY, Snapshot.Domain.CellsX,
				Snapshot.Domain.CellsY, Snapshot.Domain.GridSpacingCm, double(Finished.M4MinHeightQ8) / 256.0,
				double(Finished.M4MaxHeightQ8) / 256.0,
				Snapshot.Domain.bGlobalErosion ? TEXT("enabled") : TEXT("disabled"));
			M4StageDiagnostics +=
				bMapPublished
					? TEXT(
						  "\nHeight map: black = observed minimum, white = maximum; constant height = mid-gray. Left to right is +X, top to bottom is -Y. The image reflects the last published finite M4 preview and may be stale while inputs change. Height detail, overhang and smoothing are excluded.")
					: TEXT("\nHeight map unavailable: image allocation or upload failed.");
			if (Finished.bHasM4StageStats)
			{
				const FVoxelLandM4StageStats& S = Finished.M4StageStats;
				if (PublishM4ControlMaps(S))
				{
					M4StageDiagnostics += TEXT(
						"\nPre-erosion control maps below, left to right: continental, mountain, peaks. Black is 0, white is 65535; +X right, +Y up. Each is a 32×32 grid-sample thumbnail using floor indices; small features may be missed.");
				}
				else
				{
					M4StageDiagnostics += TEXT("\nPre-erosion control map image unavailable.");
				}
				M4StageDiagnostics += FString::Printf(
					TEXT(
						"\nObserved %d samples: continental [%u,%u], mountain [%u,%u], peaks [%u,%u] (normalized uint16); base height [%.2f,%.2f] cm before erosion."),
					S.SampleCount, uint32(S.ContinentalMin), uint32(S.ContinentalMax), uint32(S.MountainMin),
					uint32(S.MountainMax), uint32(S.PeaksMin), uint32(S.PeaksMax), double(S.BaseHeightMinQ8) / 256.0,
					double(S.BaseHeightMaxQ8) / 256.0);
				M4StageDiagnostics += FString::Printf(
					TEXT(
						"\nControl mean (normalized uint16): continental %.1f, mountain %.1f, peaks %.1f; endpoint counts (0 / 65535): continental %d / %d, mountain %d / %d, peaks %d / %d."),
					double(S.ContinentalSum) / S.SampleCount, double(S.MountainSum) / S.SampleCount,
					double(S.PeaksSum) / S.SampleCount, S.ContinentalZeroCount, S.ContinentalMaxCount,
					S.MountainZeroCount, S.MountainMaxCount, S.PeaksZeroCount, S.PeaksMaxCount);
				M4StageDiagnostics += FString::Printf(
					TEXT(
						"\nErosion change: %d / %d grid samples changed; signed range %.2f to %.2f cm, total absolute change %.2f cm. This compares final global height with base height and includes unchanged samples in the range."),
					S.ErosionChangedCount, S.SampleCount, double(S.ErosionDeltaMinQ8) / 256.0,
					double(S.ErosionDeltaMaxQ8) / 256.0, double(S.ErosionAbsDeltaSumQ8) / 256.0);
			}
			else
			{
				M4StageDiagnostics += TEXT(" Per-control observations unavailable for this grid.");
			}
		}
		else
		{
			M4StageBrush.SetResourceObject(nullptr);
			M4StageTexture = nullptr;
			M4StageDiagnostics =
				TEXT("M4 local mode: no global prepass grid is generated. Per-control ranges are not measured.");
		}
		if (Finished.bM4Inspection)
		{
			FVector CameraLocation = FVector::ZeroVector;
			FRotator CameraRotation = FRotator::ZeroRotator;
			float CameraFov = 0.f;
			FIntPoint Resolution = FIntPoint::ZeroValue;
			const FVector Target(VoxelLandM4Inspection::ChunkSizeCm / 2.0, VoxelLandM4Inspection::ChunkSizeCm / 2.0,
				(double(Finished.M4MinHeightQ8) + double(Finished.M4MaxHeightQ8)) / 512.0);
			const bool bCameraReady =
				PreviewViewport && PreviewViewport->SetInspectionCamera(Target)
				&& PreviewViewport->GetCameraDetails(CameraLocation, CameraRotation, CameraFov, Resolution);
			M4InspectionDetails = FString::Printf(
				TEXT(
					"M4 2 km inspection (visual acceptance not assessed)\nConfiguration: %s\nSeed: %lld\nInspection voxel: 800 cm; LOD 0; XY chunks -1..1; Z chunks %d..%d\nGlobal prepass height: %.2f..%.2f cm; overhang band: %d Q8 cm; height detail bound: %d Q8 cm\nRHI: %s; viewport: %d x %d\nCamera location cm: %s; rotation: %s; horizontal FOV: %.2f degrees; camera ready: %s\n"),
				*M4PreviewConfig->GetPathName(), Snapshot.Seed, Finished.M4MinChunkZ, Finished.M4MaxChunkZ,
				double(Finished.M4MinHeightQ8) / 256.0, double(Finished.M4MaxHeightQ8) / 256.0,
				Snapshot.bOverhangEnabled && Snapshot.OverhangAmplitudeQ8 > 0 ? Snapshot.OverhangBandQ8 : 0,
				Snapshot.HeightDetail.bEnabled ? Snapshot.HeightDetail.AmplitudeQ8 : 0, *FApp::GetGraphicsRHI(),
				Resolution.X, Resolution.Y, *CameraLocation.ToString(), *CameraRotation.ToString(), CameraFov,
				bCameraReady ? TEXT("yes") : TEXT("no"));
			M4InspectionAppendOverrides(M4InspectionDetails, TEXT("Continental"), Snapshot.Continental);
			M4InspectionAppendOverrides(M4InspectionDetails, TEXT("Mountain"), Snapshot.Mountain);
			M4InspectionAppendOverrides(M4InspectionDetails, TEXT("Peaks"), Snapshot.Peaks);
			M4InspectionDetails += VoxelLandM4Inspection::DescribeSnapshot(Snapshot);
			M4InspectionDetails +=
				FString::Printf(TEXT("Control graph assets: continental=%s; mountain=%s; peaks=%s\n"),
					*GetPathNameSafe(Finished.M4Continental.Get()), *GetPathNameSafe(Finished.M4Mountain.Get()),
					*GetPathNameSafe(Finished.M4Peaks.Get()));
			M4InspectionDetails += TEXT(
				"Retain these graph/config assets, their function dependencies and exact source/dependency revisions; the printed hashes identify inputs but do not embed program or LUT bytes and are not a saved world.\n");
			for (int32 Z = Finished.M4MinChunkZ; Z <= Finished.M4MaxChunkZ; ++Z)
			{
				for (int32 Y = -1; Y <= 1; ++Y)
				{
					for (int32 X = -1; X <= 1; ++X)
					{
						M4InspectionDetails += FString::Printf(
							TEXT("Chunk (%d,%d,%d): min (%d,%d,%d), max (%d,%d,%d) cm\n"), X, Y, Z,
							X * VoxelLandM4Inspection::ChunkSizeCm, Y * VoxelLandM4Inspection::ChunkSizeCm,
							Z * VoxelLandM4Inspection::ChunkSizeCm, (X + 1) * VoxelLandM4Inspection::ChunkSizeCm,
							(Y + 1) * VoxelLandM4Inspection::ChunkSizeCm, (Z + 1) * VoxelLandM4Inspection::ChunkSizeCm);
					}
				}
			}
			M4InspectionDetails += TEXT(
				"Record a screenshot and review silhouette, ridges and valleys; no visual acceptance is inferred. This is a bounded target, not a 2 km streamed horizon.");
			M4StageDiagnostics += bCameraReady
									? TEXT(" Inspection view configured; copy details and capture an image manually.")
									: TEXT(" Inspection camera unavailable; no view has been established.");
		}
	}
#if WITH_DEV_AUTOMATION_TESTS
	++PreviewPublishCountForTests;
	PublishedSeedForTests = Finished.SeedForTests;
#endif
	const double SetupCompleted = FPlatformTime::Seconds();
	PreviewFence = MakeShared<FVoxelLandPreviewFence>();
	PreviewFence->PreviewRevision = Finished.PreviewRevision;
	PreviewFence->SourceRevision = Finished.SourceRevision;
	PreviewFence->NonemptyChunks = PreviewComponents.Num();
	PreviewFence->ChunkCount = Finished.ChunkCount;
	PreviewFence->SemanticStart = SemanticChangeTime > 0.0 ? SemanticChangeTime : UploadStart;
	PreviewFence->SetupCompleted = SetupCompleted;
	PreviewFence->CompileMs = LastCompileMs;
	PreviewFence->GenerationMs = Finished.GenerationMs;
	PreviewFence->SetupMs = (SetupCompleted - UploadStart) * 1000.0;
	PreviewFence->bGPURequested = bGPURequested;
	PreviewFence->UploadObservation = MoveTemp(UploadObservation);
	PreviewFence->Fence.BeginFence(FRenderCommandFence::ESyncDepth::RenderThread);
	Status = TEXT("Preview submitted; waiting for render-thread commands.");
	if (PreviewViewport)
	{
		PreviewViewport->Invalidate();
	}
	return true;
}

void FVoxelLandGraphToolkit::PollPreviewFence()
{
	if (!PreviewFence)
	{
		return;
	}
	const TSharedPtr<FVoxelLandPreviewFence> Finished = PreviewFence;
	if (bStopped || bPIE || bReloadPaused || Finished->PreviewRevision != PreviewRevision || Finished->SourceRevision !=
	    Revision)
	{
		PreviewFence.Reset();
		return;
	}
	const double Now = FPlatformTime::Seconds();
	if (!Finished->bRenderComplete)
	{
		if (!Finished->Fence.IsFenceComplete())
		{
			return;
		}
		Finished->bRenderComplete = true;
		Finished->ReadyStatus = FString::Printf(
			TEXT(
				"Ready: render commands complete: %d nonempty / %d chunks; latest compile CPU %.1f ms, generation CPU %.1f ms, game-thread setup %.1f ms, render-thread fence observed after %.1f ms, change-to-fence observed %.1f ms."),
			Finished->NonemptyChunks, Finished->ChunkCount, Finished->CompileMs, Finished->GenerationMs,
			Finished->SetupMs,
			(Now - Finished->SetupCompleted) * 1000.0, (Now - Finished->SemanticStart) * 1000.0);
		SemanticChangeTime = 0.0;
	}
	if (!Finished->bGPURequested)
	{
		Status = Finished->ReadyStatus + TEXT(" GPU completion not measured.");
		PreviewFence.Reset();
	}
	else if (!Finished->UploadObservation)
	{
		Status = Finished->ReadyStatus + TEXT(
			         " GPU upload observation unavailable (unsupported RHI or empty preview); presentation not measured.");
		PreviewFence.Reset();
	}
	else if (
#if WITH_DEV_AUTOMATION_TESTS
		CVarVoxelLandPreviewGPUUploadHoldForTests.GetValueOnGameThread() == 0 &&
#endif
		Finished->UploadObservation->IsComplete())
	{
		Status = Finished->ReadyStatus + FString::Printf(
			         TEXT(
				         " Initial GPU buffer uploads observed complete %.1f ms after setup, %.1f ms after change (upper bounds, include polling); copy duration and presentation not measured."),
			         (Now - Finished->SetupCompleted) * 1000.0, (Now - Finished->SemanticStart) * 1000.0);
		PreviewFence.Reset();
	}
	else if (Now - Finished->SetupCompleted >= 10.0)
	{
		Status = Finished->ReadyStatus + TEXT(
			         " GPU upload observation unavailable (timed out); presentation not measured.");
		PreviewFence.Reset();
	}
	else
	{
		Status = Finished->ReadyStatus + TEXT(" GPU upload observation pending; presentation not measured.");
	}
}

void FVoxelLandGraphToolkit::ProcessCompileWork()
{
	if (!CompileWork || !CompileWork->Done.Load())
	{
		return;
	}
	const TSharedPtr<FVoxelLandCompileWork, ESPMode::ThreadSafe> Finished = MoveTemp(CompileWork);
	bool bNamedStillCurrent = true;
	UVoxelLandM4ControlGraph* Named = Finished->bNamedM4 ? Cast<UVoxelLandM4ControlGraph>(Asset) : nullptr;
	if (Finished->bNamedM4)
	{
		FVoxelLandGraphSnapshot Current;
		FString CaptureError;
		bNamedStillCurrent = Named && Named->CaptureSnapshot(Current, CaptureError)
						  && FVoxelLandProgramCodec::SourceFingerprint(Current) == Finished->SourceHash
						  && Named->ContinentalOutputId == Finished->NamedOutputIds[0]
						  && Named->MountainOutputId == Finished->NamedOutputIds[1]
						  && Named->PeaksOutputId == Finished->NamedOutputIds[2];
	}
	if (!bStopped && !bPIE && !bReloadPaused && bNamedStillCurrent && Finished->Revision == Revision
		&& Finished->TargetRevision == PreviewTargetRevision && !bCompileRequested)
	{
		if (Finished->bNamedM4)
		{
			FVoxelLandSerializedProgram* Outputs[] = {
				&Named->ContinentalProgram, &Named->MountainProgram, &Named->PeaksProgram};
			bool bChanged = false;
			for (int32 I = 0; I < 3; ++I)
			{
				FVoxelLandSerializedProgram NewValue =
					Finished->bSuccess ? MoveTemp(Finished->NamedPrograms[I]) : FVoxelLandSerializedProgram();
				bChanged |= !FVoxelLandSerializedProgram::StaticStruct()->CompareScriptStruct(Outputs[I], &NewValue, 0);
				*Outputs[I] = MoveTemp(NewValue);
			}
			if (bChanged)
			{
				Named->MarkPackageDirty();
				M4ControlGraphChanged.Broadcast(Named);
			}
			LastCompileMs = Finished->CompileMs;
			if (!Finished->bSuccess)
			{
				Status = FString::Printf(TEXT("Error; M4 preview stale: %s"), *Finished->Error);
			}
			else if (bM4Preview)
			{
				StartM4Preview();
			}
			else
			{
				Status = TEXT("M4 control outputs compiled. Select an M4 config to preview terrain.");
			}
		}
		else if (Finished->bSuccess)
		{
			ErrorDiagnostic = FVoxelLandGraphDiagnostic();
			if (Finished->bNodePreview)
			{
				NodePreviewSourceHash = Finished->SourceHash;
				NodePreviewSnapshot = MakeShared<FVoxelLandGraphSnapshot>(MoveTemp(Finished->Snapshot));
				NodePreviewPayload = MakeShared<FVoxelLandSerializedProgram>(MoveTemp(Finished->Payload));
			}
			else
			{
				StoreCompiledProgram(*Asset, MoveTemp(Finished->Payload));
			}
			LastCompileMs = Finished->CompileMs;
			StartPreview();
		}
		else
		{
			ErrorDiagnostic = Finished->Diagnostic;
			Status = FString::Printf(TEXT("Error; preview stale: %s"), *Finished->Error);
		}
	}
	if (Finished->bNamedM4 && !bNamedStillCurrent && !bStopped && !bPIE && !bReloadPaused && !bCompileRequested)
	{
		Status = TEXT("Stale M4 compile discarded: control source or output IDs changed.");
	}
	if (bCompileRequested && !bPIE)
	{
		bCompileRequested = false;
		RequestCompile();
	}
}

void FVoxelLandGraphToolkit::ProcessPreviewWork()
{
	if (!PreviewWork || !PreviewWork->Done.Load())
	{
		return;
	}
	const TSharedPtr<FVoxelLandPreviewWork, ESPMode::ThreadSafe> Finished = MoveTemp(PreviewWork);
	if (!bPIE && Finished->PreviewRevision == PreviewRevision && Finished->SourceRevision == Revision && !
	    bPreviewRequested)
	{
		PreviewCancel.Reset();
		if (!Finished->bSuccess)
		{
			Status = FString::Printf(TEXT("Error; previous preview is stale: %s"), *Finished->Error);
		}
		else if (Finished->bM4 && !IsM4PreviewCurrent(*Finished))
		{
			Status = TEXT("Stale M4 preview: configuration or control graph changed during generation.");
		}
		else if (Finished->bM4)
		{
			// Material is presentation only. Resolve the current value after generation,
			// then recheck identity because synchronous loading can re-enter editor code.
			const UVoxelLandMaterialSetup* Setup = M4PreviewConfig->MaterialSetup.LoadSynchronous();
			UMaterialInterface* Material =
				Setup && Setup->FormatVersion == 1 ? Setup->Material.LoadSynchronous() : nullptr;
			if (bStopped || bPIE || bReloadPaused || Finished->PreviewRevision != PreviewRevision
				|| Finished->SourceRevision != Revision || !IsM4PreviewCurrent(*Finished))
			{
				Status = TEXT("Stale M4 preview: source changed while loading the material.");
			}
			else
			{
				ApplyPreviewMaterial(Material);
				PublishPreview(*Finished);
			}
		}
		else if (Finished->bNoise2D)
		{
			PublishNoisePreview(*Finished);
		}
		else
		{
			PublishPreview(*Finished);
		}
	}
	if (bPreviewRequested && !bPIE)
	{
		bPreviewRequested = false;
		StartPreview();
	}
}

void FVoxelLandGraphToolkit::PollFunctionDependencies()
{
	if (!Asset || !Graph || bStopped || bPIE || bReloadPaused)
	{
		return;
	}
	const double Now = FPlatformTime::Seconds();
	if (Now < NextDependencyPoll) { return; }
	NextDependencyPoll = Now + 0.5;
	bool bHasCall = false;
	for (const UEdGraphNode* GraphNode : Graph->Nodes)
	{
		const UVoxelLandEditorNode* Node = Cast<UVoxelLandEditorNode>(GraphNode);
		if (Node && Node->Kind == int32(VoxelLandGraphFunctionCallId().D))
		{
			bHasCall = true;
			break;
		}
	}
	if (!bHasCall)
	{
		DependencyError.Reset();
		return;
	}
	FVoxelLandGraphSnapshot Snapshot;
	FVoxelLandGraphDiagnostic Diagnostic;
	FString Error;
	bool bValidDefaults = true;
	if (!CaptureMirrorWithDependencies(Snapshot, bValidDefaults, Error, &Diagnostic) || !bValidDefaults)
	{
		if (Error != DependencyError)
		{
			DependencyError = Error;
			ErrorDiagnostic = MoveTemp(Diagnostic);
			++Revision;
			CancelPreview();
			CompileDue = 0.0;
			Status = FString::Printf(TEXT("Error; preview stale: %s"), *Error);
		}
		return;
	}
	const uint64 Hash = FVoxelLandProgramCodec::SourceFingerprint(Snapshot);
	if (Hash != LastSourceHash || !DependencyError.IsEmpty())
	{
		DependencyError.Reset();
		LastSourceHash = Hash;
		++Revision;
		CancelPreview();
		SemanticChangeTime = Now;
		if (Asset->IsA<UVoxelLandGraphFunction>())
		{
			FVoxelLandEditorOperatorRegistry::NotifyFunctionChanged();
			Status = TEXT("Function dependency changed; refresh any incompatible call interfaces.");
		}
		else
		{
			CompileDue = Now + 0.2;
			Status = TEXT("Stale: function dependency changed.");
		}
	}
}

void FVoxelLandGraphToolkit::PollFunctionPalette()
{
	if (!PaletteWidget || FPlatformTime::Seconds() < NextPalettePoll) { return; }
	NextPalettePoll = FPlatformTime::Seconds() + 1.0;
	FAssetRegistryModule& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
	TArray<FAssetData> Assets;
	Registry.Get().GetAssetsByClass(UVoxelLandGraphFunction::StaticClass()->GetClassPathName(), Assets);
	TArray<FString> Paths;
	for (const FAssetData& Item : Assets) { Paths.Add(Item.GetSoftObjectPath().ToString()); }
	Paths.Sort();
	if (Paths != FunctionAssetPaths)
	{
		FunctionAssetPaths = MoveTemp(Paths);
		PaletteWidget->RefreshAllActions(true, false);
	}
}

bool FVoxelLandGraphToolkit::PollJobs(float)
{
	if (bStopped)
	{
		return false;
	}
	WorkerTasks.RemoveAll([](const TFuture<void>& Task) { return Task.IsReady(); });
	PollFunctionPalette();
	const uint64 CurrentPresentationRevision = FVoxelLandEditorOperatorRegistry::GetRevision();
	if (bM4Preview && !M4FunctionDependencies.IsEmpty()
		&& M4FunctionPresentationRevision != CurrentPresentationRevision)
	{
		M4FunctionPresentationRevision = CurrentPresentationRevision;
		StartM4Preview();
	}
	if (PaletteWidget && PresentationRevision != CurrentPresentationRevision)
	{
		PresentationRevision = CurrentPresentationRevision;
		PaletteWidget->RefreshAllActions(true, false);
	}
	if ((CompileWork && CompileWork->Done.Load()) || (PreviewWork && PreviewWork->Done.Load()) || PreviewFence)
	{
		// Recheck dependencies immediately before accepting async work or a render fence.
		NextDependencyPoll = 0.0;
	}
	PollFunctionDependencies();
	ProcessCompileWork();
	ProcessPreviewWork();
	PollPreviewFence();
	if (!bPIE && CompileDue > 0.0 && FPlatformTime::Seconds() >= CompileDue)
	{
		RequestCompile();
	}
	return true;
}
#undef LOCTEXT_NAMESPACE
