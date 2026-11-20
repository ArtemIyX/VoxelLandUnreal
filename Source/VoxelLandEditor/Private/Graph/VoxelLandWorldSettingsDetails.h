#pragma once
#include "IDetailCustomization.h"

class UVoxelLandWorldSettings;
class IPropertyHandle;

class FVoxelLandWorldSettingsDetails : public IDetailCustomization
{
public:
    virtual ~FVoxelLandWorldSettingsDetails() override;
    virtual void PendingDelete() override { UnbindGraphObservers(); }
    static TSharedRef<IDetailCustomization> MakeInstance();
    virtual void CustomizeDetails(IDetailLayoutBuilder& Builder) override;
#if WITH_DEV_AUTOMATION_TESTS
    static void SetEnabledForTests(UVoxelLandWorldSettings* Settings, FGuid Id, TSharedPtr<IPropertyHandle> Handle, bool bEnabled);
    static void SetFloatForTests(UVoxelLandWorldSettings* Settings, FGuid Id, TSharedPtr<IPropertyHandle> Handle, float Value);
    static void SetIntForTests(UVoxelLandWorldSettings* Settings, FGuid Id, TSharedPtr<IPropertyHandle> Handle, int32 Value);
    static void SetBoolForTests(UVoxelLandWorldSettings* Settings, FGuid Id, TSharedPtr<IPropertyHandle> Handle, bool Value);
    static int32 GetRefreshNotificationsForTests();
    static bool NeedsRawOverridesForTests(UVoxelLandWorldSettings* Settings);
    static FText DiagnosticForTests(UVoxelLandWorldSettings* Settings, FGuid Id);
#endif
private:
    void UnbindGraphObservers();
    FDelegateHandle PropertyChangedHandle;
    FDelegateHandle TransactedHandle;
};
