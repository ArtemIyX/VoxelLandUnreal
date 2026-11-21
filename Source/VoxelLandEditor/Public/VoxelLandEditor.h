#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

class FVoxelLandEditorModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
	// Open Slate widgets/toolkits must not outlive a dynamically unloaded code image.
	virtual bool SupportsDynamicReloading() override { return false; }
private:
	void OnObjectPreSave(UObject* Object, class FObjectPreSaveContext Context);
	void OnFunctionPropertyChanged(UObject* Object, struct FPropertyChangedEvent& Event);
	void OnFunctionTransacted(UObject* Object, const class FTransactionObjectEvent& Event);
	void OnFunctionObjectsReplaced(const TMap<UObject*, UObject*>& Replacements);
	FDelegateHandle PreSaveHandle;
	FDelegateHandle FunctionPropertyHandle;
	FDelegateHandle FunctionTransactionHandle;
	FDelegateHandle FunctionReplacedHandle;
	TSharedPtr<class FSlateStyleSet> Style;
	TSharedPtr<class FVoxelLandGraphPinFactory> PinFactory;
	TSharedPtr<class IAssetTypeActions> GraphAssetActions;
	TSharedPtr<class IAssetTypeActions> FunctionAssetActions;
};
