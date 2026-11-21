#include "VoxelLandEditor.h"
#include "VoxelLandM4EditorBake.h"
#include "Operators/VoxelLandOperator.h"
#include "AssetToolsModule.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "AssetTypeActions_Base.h"
#include "Data/Assets/VoxelLandTerrainGraph.h"
#include "Data/Assets/VoxelLandGraphFunction.h"
#include "Data/Assets/VoxelLandM4WorldConfig.h"
#include "Data/Structs/VoxelLandM4WorldSnapshot.h"
#include "Graph/VoxelLandGraphToolkit.h"
#include "Graph/VoxelLandGraphPinFactory.h"
#include "Graph/VoxelLandWorldSettingsDetails.h"
#include "PropertyEditorModule.h"
#include "Graph/VoxelLandEditorOperator.h"
#include "Graph/VoxelLandGraphCompiler.h"
#include "Graph/VoxelLandProgramCodec.h"
#include "Generation/VoxelLandGenerator.h"
#include "Generation/VoxelLandM4ControlGrid.h"
#include "Generation/VoxelLandM4CurveBake.h"
#include "Async/Async.h"
#include "UObject/ObjectSaveContext.h"
#include "VoxelLandAlgo.h"
#include "Operators/VoxelLandSquareOperator.h"
#include "Operators/VoxelLandExampleFilterOperator.h"
#include "Interfaces/IPluginManager.h"
#include "Styling/SlateStyle.h"
#include "Styling/SlateStyleRegistry.h"
#include "Brushes/SlateImageBrush.h"

#define LOCTEXT_NAMESPACE "FVoxelLandEditorModule"

namespace VoxelLandM4Editor
{
bool BakeWorldConfig(UVoxelLandM4WorldConfig& Config, FString& Error)
{
	if (!IsInGameThread())
	{
		Error = TEXT("M4 configuration baking requires the game thread");
		return false;
	}
	FVoxelLandM4WorldSnapshot Snapshot;
	if (!Config.CaptureSnapshot(Snapshot, Error))
	{
		Config.ClearBakedCurves();
		return false;
	}
	TArray<int32> Baked[3];
	const bool bBaked = Async(EAsyncExecution::ThreadPool, [&Snapshot, &Baked, &Error]() {
		const FVoxelLandM4ControlSnapshot* Channels[] = {&Snapshot.Continental, &Snapshot.Mountain, &Snapshot.Peaks};
		for (int32 Index = 0; Index < UE_ARRAY_COUNT(Channels); ++Index)
		{
			const FVoxelLandM4ControlSnapshot& Channel = *Channels[Index];
			TSharedPtr<const FVoxelLandProgram, ESPMode::ThreadSafe> Program;
			if (!FVoxelLandProgramCodec::Load(Channel.Source, Channel.Payload, Program, Error))
			{
				return false;
			}
			if (const FVoxelLandError Validation = VoxelLandM4ControlGrid::ValidateProgram(*Program))
			{
				Error = Validation.Message;
				return false;
			}
			TArray<FVoxelLandM4CurveKey> Keys;
			Keys.Reserve(Channel.CurveKeys.Num());
			for (const FVoxelLandM4CurveAuthoringKey& Key : Channel.CurveKeys)
			{
				FVoxelLandM4CurveKey& Target = Keys.AddDefaulted_GetRef();
				Target.InputQ16 = uint16(Key.InputQ16);
				Target.Output = Key.OutputQ8;
			}
			TSharedPtr<const FVoxelLandM4CurveLut, ESPMode::ThreadSafe> Curve;
			if (!VoxelLandM4CurveBake::Bake(Keys, -512000000, 512000000, Baked[Index], Curve, Error))
			{
				return false;
			}
		}
		return true;
	}).Get();
	if (!bBaked
		|| !Config.SetBakedCurves(Baked[0], Baked[1], Baked[2], int32(VoxelLandM4CurveBake::AlgorithmVersion), Error))
	{
		// Never retain previously valid LUTs when the current source cannot be
		// baked. This also protects direct callers, not only the PreSave hook.
		Config.ClearBakedCurves();
		return false;
	}
	return true;
}
}

namespace
{
class FVoxelLandGraphAssetActions : public FAssetTypeActions_Base
{
public:
	virtual FText GetName() const override { return LOCTEXT("GraphAsset", "VoxelLand Terrain Graph"); }
	virtual FColor GetTypeColor() const override { return FColor(67, 158, 92); }
	virtual UClass* GetSupportedClass() const override { return UVoxelLandTerrainGraph::StaticClass(); }
	virtual uint32 GetCategories() override { return EAssetTypeCategories::Misc; }

	virtual void OpenAssetEditor(const TArray<UObject*>& Objects, TSharedPtr<IToolkitHost> Host) override
	{
		for (UObject* Object : Objects)
		{
			if (UVoxelLandTerrainGraph* Graph = Cast<UVoxelLandTerrainGraph>(Object))
			{
				TSharedRef<FVoxelLandGraphToolkit> Editor = MakeShared<FVoxelLandGraphToolkit>();
				Editor->Open(Graph, Host.IsValid() ? EToolkitMode::WorldCentric : EToolkitMode::Standalone, Host);
				Editors.Add(Editor);
			}
		}
	}

	void StopEditors()
	{
		for (const TWeakPtr<FVoxelLandGraphToolkit>& Weak : Editors)
		{
			if (TSharedPtr<FVoxelLandGraphToolkit> Editor = Weak.Pin())
			{
				Editor->Stop();
				if (!Editor->CloseWindow(EAssetEditorCloseReason::AssetEditorHostClosed))
				{
					UE_LOG(LogTemp, Warning, TEXT("VoxelLand graph editor declined to close during module shutdown"));
				}
			}
		}
		Editors.Reset();
	}

private:
	TArray<TWeakPtr<FVoxelLandGraphToolkit>> Editors;
};

class FVoxelLandFunctionAssetActions : public FVoxelLandGraphAssetActions
{
public:
	virtual FText GetName() const override { return LOCTEXT("FunctionAsset", "VoxelLand Graph Function"); }
	virtual UClass* GetSupportedClass() const override { return UVoxelLandGraphFunction::StaticClass(); }
	virtual FColor GetTypeColor() const override { return FColor(130, 85, 173); }
};
}

void FVoxelLandEditorModule::StartupModule()
{
	const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("VoxelLand"));
	if (Plugin)
	{
		Style = MakeShared<FSlateStyleSet>(TEXT("VoxelLandEditorStyle"));
		Style->SetContentRoot(Plugin->GetBaseDir() / TEXT("Resources"));
		const auto AddIcon = [this](const TCHAR* ClassName, const TCHAR* FileName) {
			const FString Path = Style->RootToContentDir(FileName, TEXT(".svg"));
			Style->Set(*FString::Printf(TEXT("ClassIcon.%s"), ClassName),
				new FSlateVectorImageBrush(Path, FVector2D(16.0f, 16.0f)));
			Style->Set(*FString::Printf(TEXT("ClassThumbnail.%s"), ClassName),
				new FSlateVectorImageBrush(Path, FVector2D(64.0f, 64.0f)));
		};
		AddIcon(TEXT("VoxelLandTerrainGraph"), TEXT("TerrainGraph"));
		AddIcon(TEXT("VoxelLandWorldSettings"), TEXT("WorldSettings"));
		AddIcon(TEXT("VoxelLandM4WorldConfig"), TEXT("WorldSettings"));
		AddIcon(TEXT("VoxelLandMaterialSetup"), TEXT("MaterialSetup"));
		FSlateStyleRegistry::RegisterSlateStyle(*Style);
	}
	FModuleManager::LoadModuleChecked<FPropertyEditorModule>(TEXT("PropertyEditor")).RegisterCustomClassLayout(
		TEXT("VoxelLandWorldSettings"),
		FOnGetDetailCustomizationInstance::CreateStatic(&FVoxelLandWorldSettingsDetails::MakeInstance));
	PinFactory = MakeShared<FVoxelLandGraphPinFactory>();
	FEdGraphUtilities::RegisterVisualPinFactory(PinFactory);
	FModuleManager::LoadModuleChecked<FVoxelLandAlgoModule>(TEXT("VoxelLandAlgo"));
	FVoxelLandEditorOperatorPresentation Square;
	Square.OperatorId = VoxelLandSquareOperatorId();
	Square.Category = LOCTEXT("Extensions", "C++ extensions");
	Square.DisplayName = LOCTEXT("Square", "Square");
	Square.Tooltip = LOCTEXT("SquareTooltip", "Example registered scalar C++ operator.");
	FVoxelLandGraphPinRecord Input;
	Input.Name = TEXT("Value");
	Input.Type = EVoxelLandGraphValueType::Float;
	Square.Inputs.Add(MoveTemp(Input));
	Square.InputTooltips.Add(LOCTEXT("SquareInputTip", "Value to multiply by itself."));
	Square.OutputTooltip = LOCTEXT("SquareOutputTip", "The square of the input value.");
	FString Error;
	if (!FVoxelLandEditorOperatorRegistry::Register(Square, Error))
	{
		UE_LOG(LogTemp, Warning, TEXT("VoxelLand square editor presentation unavailable: %s"), *Error);
	}
	FVoxelLandEditorOperatorPresentation Filter;
	Filter.OperatorId = VoxelLandExampleFilterOperatorId();
	Filter.Category = LOCTEXT("FilterExtensions", "C++ filters");
	Filter.DisplayName = LOCTEXT("ExampleFilter", "Example X Average Filter");
	Filter.Tooltip = LOCTEXT("ExampleFilterTooltip",
		"One-cell quantized three-sample average along X; connect directly to Density Output.");
	FVoxelLandGraphPinRecord Density;
	Density.Name = TEXT("Density");
	Density.Type = EVoxelLandGraphValueType::Float;
	Filter.Inputs.Add(MoveTemp(Density));
	Filter.InputTooltips.Add(LOCTEXT("ExampleFilterInputTip",
		"Source density in centimeters; negative values represent solid terrain."));
	Filter.OutputTooltip = LOCTEXT("ExampleFilterOutputTip", "Quantized density averaged along the X axis.");
	if (!FVoxelLandEditorOperatorRegistry::Register(Filter, Error))
	{
		UE_LOG(LogTemp, Warning, TEXT("VoxelLand filter presentation unavailable: %s"), *Error);
	}
	IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools")).Get();
	GraphAssetActions = MakeShared<FVoxelLandGraphAssetActions>();
	AssetTools.RegisterAssetTypeActions(GraphAssetActions.ToSharedRef());
	FunctionAssetActions = MakeShared<FVoxelLandFunctionAssetActions>();
	AssetTools.RegisterAssetTypeActions(FunctionAssetActions.ToSharedRef());
	PreSaveHandle = FCoreUObjectDelegates::OnObjectPreSave.AddRaw(this, &FVoxelLandEditorModule::OnObjectPreSave);
	FunctionPropertyHandle = FCoreUObjectDelegates::OnObjectPropertyChanged.AddRaw(this,
		&FVoxelLandEditorModule::OnFunctionPropertyChanged);
	FunctionTransactionHandle = FCoreUObjectDelegates::OnObjectTransacted.AddRaw(this,
		&FVoxelLandEditorModule::OnFunctionTransacted);
	FunctionReplacedHandle = FCoreUObjectDelegates::OnObjectsReplaced.AddRaw(this,
		&FVoxelLandEditorModule::OnFunctionObjectsReplaced);
}

void FVoxelLandEditorModule::ShutdownModule()
{
	FCoreUObjectDelegates::OnObjectPreSave.Remove(PreSaveHandle);
	FCoreUObjectDelegates::OnObjectPropertyChanged.Remove(FunctionPropertyHandle);
	FCoreUObjectDelegates::OnObjectTransacted.Remove(FunctionTransactionHandle);
	FCoreUObjectDelegates::OnObjectsReplaced.Remove(FunctionReplacedHandle);
	if (GraphAssetActions.IsValid())
	{
		StaticCastSharedPtr<FVoxelLandGraphAssetActions>(GraphAssetActions)->StopEditors();
	}
	if (FunctionAssetActions.IsValid())
	{
		StaticCastSharedPtr<FVoxelLandFunctionAssetActions>(FunctionAssetActions)->StopEditors();
	}
	if (GraphAssetActions.IsValid() && FModuleManager::Get().IsModuleLoaded(TEXT("AssetTools")))
	{
		FModuleManager::GetModuleChecked<FAssetToolsModule>(TEXT("AssetTools")).Get().UnregisterAssetTypeActions(
			GraphAssetActions.ToSharedRef());
	}
	if (FunctionAssetActions.IsValid() && FModuleManager::Get().IsModuleLoaded(TEXT("AssetTools")))
	{
		FModuleManager::GetModuleChecked<FAssetToolsModule>(TEXT("AssetTools")).Get().UnregisterAssetTypeActions(
			FunctionAssetActions.ToSharedRef());
	}
	FunctionAssetActions.Reset();
	GraphAssetActions.Reset();
	if (FModuleManager::Get().IsModuleLoaded(TEXT("PropertyEditor")))
	{
		FModuleManager::GetModuleChecked<FPropertyEditorModule>(TEXT("PropertyEditor")).UnregisterCustomClassLayout(
			TEXT("VoxelLandWorldSettings"));
	}
	FEdGraphUtilities::UnregisterVisualPinFactory(PinFactory);
	PinFactory.Reset();
	FVoxelLandEditorOperatorRegistry::Unregister(VoxelLandExampleFilterOperatorId(), 1);
	FVoxelLandEditorOperatorRegistry::Unregister(VoxelLandSquareOperatorId(), 1);
	FVoxelLandEditorOperatorRegistry::Clear();
	if (Style)
	{
		FSlateStyleRegistry::UnRegisterSlateStyle(*Style);
		Style.Reset();
	}
}

void FVoxelLandEditorModule::OnFunctionPropertyChanged(UObject* Object, FPropertyChangedEvent&)
{
	if (IsInGameThread() && Object && (Object->IsA<UVoxelLandGraphFunction>() || Object->GetTypedOuter<
		                                   UVoxelLandGraphFunction>()))
	{
		FVoxelLandEditorOperatorRegistry::NotifyFunctionChanged();
	}
}

void FVoxelLandEditorModule::OnFunctionTransacted(UObject* Object, const FTransactionObjectEvent&)
{
	if (IsInGameThread() && Object && (Object->IsA<UVoxelLandGraphFunction>() || Object->GetTypedOuter<
		                                   UVoxelLandGraphFunction>()))
	{
		FVoxelLandEditorOperatorRegistry::NotifyFunctionChanged();
	}
}

void FVoxelLandEditorModule::OnFunctionObjectsReplaced(const TMap<UObject*, UObject*>& Replacements)
{
	if (!IsInGameThread()) { return; }
	for (const auto& Entry : Replacements)
	{
		if ((Entry.Key && (Entry.Key->IsA<UVoxelLandGraphFunction>() || Entry.Key->GetTypedOuter<
			                   UVoxelLandGraphFunction>())) ||
		    (Entry.Value && (Entry.Value->IsA<UVoxelLandGraphFunction>() || Entry.Value->GetTypedOuter<
			                     UVoxelLandGraphFunction>())))
		{
			FVoxelLandEditorOperatorRegistry::NotifyFunctionChanged();
			return;
		}
	}
}

void FVoxelLandEditorModule::OnObjectPreSave(UObject* Object, FObjectPreSaveContext Context)
{
	if (UVoxelLandM4WorldConfig* Config = Cast<UVoxelLandM4WorldConfig>(Object))
	{
		if (!Context.IsFirstConcurrentSave())
		{
			return;
		}
		FString Error;
		if (!VoxelLandM4Editor::BakeWorldConfig(*Config, Error))
		{
			if (Context.IsCooking())
			{
				UE_LOG(LogTemp, Error, TEXT("VoxelLand M4 configuration %s cannot cook: %s"), *Config->GetPathName(),
					*Error);
			}
			else
			{
				UE_LOG(LogTemp, Warning, TEXT("VoxelLand M4 configuration %s is not baked: %s"), *Config->GetPathName(),
					*Error);
			}
		}
		return;
	}
	UVoxelLandTerrainGraph* Graph = Cast<UVoxelLandTerrainGraph>(Object);
	if (!Graph || Graph->IsA<UVoxelLandGraphFunction>() || !Context.IsFirstConcurrentSave())
	{
		return;
	}
	FString Error;
	FVoxelLandGraphSnapshot Snapshot;
	bool bOk = Graph->CaptureSnapshot(Snapshot, Error);
	FVoxelLandSerializedProgram Payload;
	if (bOk)
	{
		TFuture<bool> Work = Async(EAsyncExecution::ThreadPool, [&Snapshot, &Payload, &Error]() {
			const FVoxelLandOperatorRegistrySnapshot Providers = FVoxelLandOperatorRegistry::Snapshot();
			FVoxelLandProgramDraft Draft;
			if (!FVoxelLandGraphCompiler::Compile(Snapshot, Providers, Draft, Error))
			{
				return false;
			}
			TSharedPtr<const FVoxelLandProgram, ESPMode::ThreadSafe> Program;
			const FVoxelLandError Validation = FVoxelLandProgram::Validate(Draft, Providers, Program);
			if (Validation)
			{
				Error = Validation.Message;
				return false;
			}
			return FVoxelLandProgramCodec::Pack(Snapshot, *Program, Payload, Error);
		});
		bOk = Work.Get();
	}
	if (bOk)
	{
		Graph->CompiledProgram = MoveTemp(Payload);
	}
	else if (Context.IsCooking())
	{
		Graph->CompiledProgram = FVoxelLandSerializedProgram();
		UE_LOG(LogTemp, Error, TEXT("VoxelLand graph %s cannot cook: %s"), *Graph->GetPathName(), *Error);
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("VoxelLand graph %s is not compiled: %s"), *Graph->GetPathName(), *Error);
	}
}
#undef LOCTEXT_NAMESPACE
IMPLEMENT_MODULE(FVoxelLandEditorModule, VoxelLandEditor)
