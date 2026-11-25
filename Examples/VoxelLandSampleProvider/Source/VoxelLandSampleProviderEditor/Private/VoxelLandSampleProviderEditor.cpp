#include "Graph/VoxelLandEditorOperator.h"
#include "Modules/ModuleManager.h"
#include "VoxelLandSampleProviderId.h"

#define LOCTEXT_NAMESPACE "VoxelLandSampleProviderEditor"

DEFINE_LOG_CATEGORY_STATIC(LogVoxelLandSampleProviderEditor, Log, All);

class FVoxelLandSampleProviderEditorModule final : public IModuleInterface
{
public:
	bool SupportsDynamicReloading() override { return false; }

	void StartupModule() override
	{
		FModuleManager::LoadModuleChecked<IModuleInterface>(TEXT("VoxelLandEditor"));
		FModuleManager::LoadModuleChecked<IModuleInterface>(TEXT("VoxelLandSampleProvider"));
		FVoxelLandEditorOperatorPresentation Presentation;
		Presentation.OperatorId = VoxelLandSampleDoubleId();
		Presentation.Version = 1;
		Presentation.Category = LOCTEXT("Category", "Sample provider");
		Presentation.DisplayName = LOCTEXT("Name", "Double");
		Presentation.Tooltip = LOCTEXT("Tooltip", "Multiply the input by two; non-finite results fail evaluation.");
		FVoxelLandGraphPinRecord Input;
		Input.Name = TEXT("Value");
		Input.Type = EVoxelLandGraphValueType::Float;
		Input.FloatDefault = 0.0f;
		Presentation.Inputs.Add(MoveTemp(Input));
		Presentation.OutputName = TEXT("Result");
		Presentation.OutputType = EVoxelLandGraphValueType::Float;
		FString Error;
		bRegistered = FVoxelLandEditorOperatorRegistry::Register(Presentation, Error);
		if (!bRegistered)
		{
			UE_LOG(LogVoxelLandSampleProviderEditor, Error, TEXT("Could not register sample presentation: %s"), *Error);
		}
	}

	void ShutdownModule() override
	{
		if (bRegistered)
		{
			if (!FVoxelLandEditorOperatorRegistry::Unregister(VoxelLandSampleDoubleId(), 1))
			{
				UE_LOG(LogVoxelLandSampleProviderEditor, Warning, TEXT("Could not unregister sample presentation"));
			}
			bRegistered = false;
		}
	}

private:
	bool bRegistered = false;
};

IMPLEMENT_MODULE(FVoxelLandSampleProviderEditorModule, VoxelLandSampleProviderEditor)

#undef LOCTEXT_NAMESPACE
