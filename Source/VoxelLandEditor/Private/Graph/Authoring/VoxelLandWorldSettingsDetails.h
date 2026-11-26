#pragma once
#include "IDetailCustomization.h"

class UVoxelLandWorldConfig;
class IPropertyHandle;

class FVoxelLandWorldSettingsDetails : public IDetailCustomization
{
public:
	virtual ~FVoxelLandWorldSettingsDetails() override;
	virtual void PendingDelete() override { UnbindGraphObservers(); }
	static TSharedRef<IDetailCustomization> MakeInstance();
	virtual void CustomizeDetails(IDetailLayoutBuilder& Builder) override;

#if WITH_DEV_AUTOMATION_TESTS
	static void SetEnabledForTests(
		UVoxelLandWorldConfig* Settings, FGuid Id, TSharedPtr<IPropertyHandle> Handle, bool bEnabled);
	static void SetFloatForTests(
		UVoxelLandWorldConfig* Settings, FGuid Id, TSharedPtr<IPropertyHandle> Handle, float Value);
	static void SetIntForTests(
		UVoxelLandWorldConfig* Settings, FGuid Id, TSharedPtr<IPropertyHandle> Handle, int32 Value);
	static void SetBoolForTests(
		UVoxelLandWorldConfig* Settings, FGuid Id, TSharedPtr<IPropertyHandle> Handle, bool Value);
	static int32 GetRefreshNotificationsForTests();
	static bool NeedsRawOverridesForTests(UVoxelLandWorldConfig* Settings);
	static FText DiagnosticForTests(UVoxelLandWorldConfig* Settings, FGuid Id);
#endif

private:
	void UnbindGraphObservers();
	FDelegateHandle PropertyChangedHandle;
	FDelegateHandle TransactedHandle;
};