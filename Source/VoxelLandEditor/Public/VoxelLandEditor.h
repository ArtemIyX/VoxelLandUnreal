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
	FDelegateHandle PreSaveHandle;
	TSharedPtr<class FVoxelLandGraphPinFactory> PinFactory;
	TSharedPtr<class IAssetTypeActions> GraphAssetActions;
};
