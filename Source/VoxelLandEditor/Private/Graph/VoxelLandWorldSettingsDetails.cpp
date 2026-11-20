#include "Graph/VoxelLandWorldSettingsDetails.h"
#include "Graph/VoxelLandFloatTypeInterface.h"
#include "Data/Assets/VoxelLandWorldSettings.h"
#include "Data/Assets/VoxelLandTerrainGraph.h"
#include "DetailCategoryBuilder.h"
#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "PropertyHandle.h"
#include "IPropertyUtilities.h"
#include "ScopedTransaction.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SNumericEntryBox.h"
#include "Widgets/Text/STextBlock.h"

namespace
{
#if WITH_DEV_AUTOMATION_TESTS
int32 RefreshNotificationsForTests = 0;
#endif
const FVoxelLandGraphParameter* FindParameter(const UVoxelLandWorldSettings* Settings, const FGuid& Id)
{
    return Settings && Settings->Graph ? Settings->Graph->Parameters.FindByPredicate([&Id](const FVoxelLandGraphParameter& P) { return P.Id == Id; }) : nullptr;
}
FVoxelLandParameterOverride* FindOverride(UVoxelLandWorldSettings* Settings, const FGuid& Id)
{
    return Settings ? Settings->Overrides.FindByPredicate([&Id](const FVoxelLandParameterOverride& O) { return O.ParameterId == Id; }) : nullptr;
}
void EditOverride(TWeakObjectPtr<UVoxelLandWorldSettings> Weak, FGuid Id, TSharedPtr<IPropertyHandle> Handle,
    TFunction<void(UVoxelLandWorldSettings&, const FVoxelLandGraphParameter&)> Edit)
{
    UVoxelLandWorldSettings* Settings = Weak.Get();
    const FVoxelLandGraphParameter* Parameter = FindParameter(Settings, Id);
    if (!Settings || !Parameter || !Handle.IsValid())
    {
        return;
    }
    const FScopedTransaction Transaction(NSLOCTEXT("VoxelLand", "EditParameterOverride", "Edit terrain parameter override"));
    Handle->NotifyPreChange();
    Settings->Modify();
    Edit(*Settings, *Parameter);
    Handle->NotifyPostChange(EPropertyChangeType::ValueSet);
    Handle->NotifyFinishedChangingProperties();
}
void ToggleOverride(TWeakObjectPtr<UVoxelLandWorldSettings> Weak, FGuid Id, TSharedPtr<IPropertyHandle> Handle, bool bEnabled)
{
    if ((FindOverride(Weak.Get(), Id) != nullptr) == bEnabled)
    {
        return;
    }
    EditOverride(Weak, Id, Handle, [Id, bEnabled](UVoxelLandWorldSettings& Settings, const FVoxelLandGraphParameter& P)
    {
        Settings.Overrides.RemoveAll([Id](const FVoxelLandParameterOverride& O) { return O.ParameterId == Id; });
        if (bEnabled)
        {
            FVoxelLandParameterOverride& O = Settings.Overrides.AddDefaulted_GetRef();
            O.ParameterId = Id; O.Type = P.Type; O.FloatValue = P.FloatDefault; O.IntValue = P.IntDefault; O.BoolValue = P.BoolDefault;
        }
    });
}
void SetFloat(TWeakObjectPtr<UVoxelLandWorldSettings> Weak, FGuid Id, TSharedPtr<IPropertyHandle> Handle, float Value)
{
    const FVoxelLandGraphParameter* P = FindParameter(Weak.Get(), Id);
    const FVoxelLandParameterOverride* Current = FindOverride(Weak.Get(), Id);
    if (!P || P->Type != EVoxelLandGraphValueType::Float || !Current || Current->Type != P->Type || !FMath::IsFinite(Value) || Current->FloatValue == Value ||
        (P->bHasRange && (Value < P->FloatMin || Value > P->FloatMax)))
    {
        return;
    }
    EditOverride(Weak, Id, Handle, [Id, Value](UVoxelLandWorldSettings& S, const FVoxelLandGraphParameter&)
    {
        if (FVoxelLandParameterOverride* O = FindOverride(&S, Id)) { O->FloatValue = Value; }
    });
}
void SetInt(TWeakObjectPtr<UVoxelLandWorldSettings> Weak, FGuid Id, TSharedPtr<IPropertyHandle> Handle, int32 Value)
{
    const FVoxelLandGraphParameter* P = FindParameter(Weak.Get(), Id);
    const FVoxelLandParameterOverride* Current = FindOverride(Weak.Get(), Id);
    if (!P || P->Type != EVoxelLandGraphValueType::Int32 || !Current || Current->Type != P->Type || Current->IntValue == Value ||
        (P->bHasRange && (Value < P->IntMin || Value > P->IntMax)))
    {
        return;
    }
    EditOverride(Weak, Id, Handle, [Id, Value](UVoxelLandWorldSettings& S, const FVoxelLandGraphParameter&)
    {
        if (FVoxelLandParameterOverride* O = FindOverride(&S, Id)) { O->IntValue = Value; }
    });
}
void SetBool(TWeakObjectPtr<UVoxelLandWorldSettings> Weak, FGuid Id, TSharedPtr<IPropertyHandle> Handle, bool Value)
{
    const FVoxelLandGraphParameter* P = FindParameter(Weak.Get(), Id);
    const FVoxelLandParameterOverride* Current = FindOverride(Weak.Get(), Id);
    if (!P || P->Type != EVoxelLandGraphValueType::Bool || !Current || Current->Type != P->Type || Current->BoolValue == Value)
    {
        return;
    }
    EditOverride(Weak, Id, Handle, [Id, Value](UVoxelLandWorldSettings& S, const FVoxelLandGraphParameter&)
    {
        if (FVoxelLandParameterOverride* O = FindOverride(&S, Id)) { O->BoolValue = Value; }
    });
}
bool NeedsRawOverrides(UVoxelLandWorldSettings* Settings)
{
    if (!Settings) { return false; }
    TSet<FGuid> Seen;
    for (const FVoxelLandParameterOverride& Override : Settings->Overrides)
    {
        if (!FindParameter(Settings, Override.ParameterId) || Seen.Contains(Override.ParameterId)) { return true; }
        Seen.Add(Override.ParameterId);
    }
    return false;
}
FText Diagnostic(TWeakObjectPtr<UVoxelLandWorldSettings> Weak, FGuid Id)
{
    const FVoxelLandGraphParameter* P = FindParameter(Weak.Get(), Id);
    const FVoxelLandParameterOverride* O = FindOverride(Weak.Get(), Id);
    if (!P || !O) { return FText::GetEmpty(); }
    if (O->Type != P->Type) { return NSLOCTEXT("VoxelLand", "OverrideTypeMismatch", "Type changed: disable and re-enable this override."); }
    if (P->Type == EVoxelLandGraphValueType::Float && (!FMath::IsFinite(O->FloatValue) || (P->bHasRange && (O->FloatValue < P->FloatMin || O->FloatValue > P->FloatMax))))
    {
        return NSLOCTEXT("VoxelLand", "FloatRange", "Override is outside the parameter range.");
    }
    if (P->Type == EVoxelLandGraphValueType::Int32 && P->bHasRange && (O->IntValue < P->IntMin || O->IntValue > P->IntMax))
    {
        return NSLOCTEXT("VoxelLand", "IntRange", "Override is outside the parameter range.");
    }
    return FText::GetEmpty();
}
TSharedRef<SWidget> ValueWidget(TWeakObjectPtr<UVoxelLandWorldSettings> Weak, const FVoxelLandGraphParameter& Parameter, TSharedPtr<IPropertyHandle> Handle)
{
    const FGuid Id = Parameter.Id;
    auto Enabled = [Weak, Id]() { const FVoxelLandParameterOverride* O = FindOverride(Weak.Get(), Id); const FVoxelLandGraphParameter* P = FindParameter(Weak.Get(), Id); return O && P && O->Type == P->Type; };
    if (Parameter.Type == EVoxelLandGraphValueType::Bool)
    {
        return SNew(SCheckBox).IsEnabled_Lambda(Enabled)
            .IsChecked_Lambda([Weak, Id]() { const FVoxelLandParameterOverride* O = FindOverride(Weak.Get(), Id); const FVoxelLandGraphParameter* P = FindParameter(Weak.Get(), Id); return (O ? O->BoolValue : P && P->BoolDefault) ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
            .OnCheckStateChanged_Lambda([Weak, Id, Handle](ECheckBoxState State) { SetBool(Weak, Id, Handle, State == ECheckBoxState::Checked); });
    }
    if (Parameter.Type == EVoxelLandGraphValueType::Int32)
    {
        return SNew(SNumericEntryBox<int32>).IsEnabled_Lambda(Enabled)
            .Value_Lambda([Weak, Id]() -> TOptional<int32> { const FVoxelLandParameterOverride* O = FindOverride(Weak.Get(), Id); const FVoxelLandGraphParameter* P = FindParameter(Weak.Get(), Id); return O ? TOptional<int32>(O->IntValue) : P ? TOptional<int32>(P->IntDefault) : TOptional<int32>(); })
            .OnValueCommitted_Lambda([Weak, Id, Handle](int32 Value, ETextCommit::Type) { SetInt(Weak, Id, Handle, Value); });
    }
    return SNew(SNumericEntryBox<float>).TypeInterface(MakeShared<FVoxelLandFloatTypeInterface>()).IsEnabled_Lambda(Enabled)
        .Value_Lambda([Weak, Id]() -> TOptional<float> { const FVoxelLandParameterOverride* O = FindOverride(Weak.Get(), Id); const FVoxelLandGraphParameter* P = FindParameter(Weak.Get(), Id); return O ? TOptional<float>(O->FloatValue) : P ? TOptional<float>(P->FloatDefault) : TOptional<float>(); })
        .OnValueCommitted_Lambda([Weak, Id, Handle](float Value, ETextCommit::Type) { SetFloat(Weak, Id, Handle, Value); });
}
}

#if WITH_DEV_AUTOMATION_TESTS
void FVoxelLandWorldSettingsDetails::SetEnabledForTests(UVoxelLandWorldSettings* Settings, FGuid Id, TSharedPtr<IPropertyHandle> Handle, bool bEnabled)
{
    ToggleOverride(Settings, Id, Handle, bEnabled);
}
void FVoxelLandWorldSettingsDetails::SetFloatForTests(UVoxelLandWorldSettings* Settings, FGuid Id, TSharedPtr<IPropertyHandle> Handle, float Value)
{
    SetFloat(Settings, Id, Handle, Value);
}
void FVoxelLandWorldSettingsDetails::SetIntForTests(UVoxelLandWorldSettings* Settings, FGuid Id, TSharedPtr<IPropertyHandle> Handle, int32 Value)
{
    SetInt(Settings, Id, Handle, Value);
}
void FVoxelLandWorldSettingsDetails::SetBoolForTests(UVoxelLandWorldSettings* Settings, FGuid Id, TSharedPtr<IPropertyHandle> Handle, bool Value)
{
    SetBool(Settings, Id, Handle, Value);
}
int32 FVoxelLandWorldSettingsDetails::GetRefreshNotificationsForTests()
{
    return RefreshNotificationsForTests;
}
bool FVoxelLandWorldSettingsDetails::NeedsRawOverridesForTests(UVoxelLandWorldSettings* Settings)
{
    return NeedsRawOverrides(Settings);
}
FText FVoxelLandWorldSettingsDetails::DiagnosticForTests(UVoxelLandWorldSettings* Settings, FGuid Id)
{
    return Diagnostic(Settings, Id);
}
#endif

FVoxelLandWorldSettingsDetails::~FVoxelLandWorldSettingsDetails()
{
    UnbindGraphObservers();
}
void FVoxelLandWorldSettingsDetails::UnbindGraphObservers()
{
    FCoreUObjectDelegates::OnObjectPropertyChanged.Remove(PropertyChangedHandle);
    FCoreUObjectDelegates::OnObjectTransacted.Remove(TransactedHandle);
    PropertyChangedHandle.Reset();
    TransactedHandle.Reset();
}
TSharedRef<IDetailCustomization> FVoxelLandWorldSettingsDetails::MakeInstance()
{
    return MakeShared<FVoxelLandWorldSettingsDetails>();
}
void FVoxelLandWorldSettingsDetails::CustomizeDetails(IDetailLayoutBuilder& Builder)
{
    TArray<TWeakObjectPtr<UObject>> Objects;
    Builder.GetObjectsBeingCustomized(Objects);
    if (Objects.Num() != 1) { return; }
    TWeakObjectPtr<UVoxelLandWorldSettings> Settings = Cast<UVoxelLandWorldSettings>(Objects[0].Get());
    if (!Settings.IsValid()) { return; }
    TSharedRef<IPropertyHandle> Overrides = Builder.GetProperty(GET_MEMBER_NAME_CHECKED(UVoxelLandWorldSettings, Overrides));
    if (!NeedsRawOverrides(Settings.Get()))
    {
        Builder.HideProperty(Overrides);
    }
    TSharedRef<IPropertyHandle> GraphHandle = Builder.GetProperty(GET_MEMBER_NAME_CHECKED(UVoxelLandWorldSettings, Graph));
    TWeakPtr<IPropertyUtilities> Utilities = Builder.GetPropertyUtilities();
    UnbindGraphObservers();
    auto RefreshGraph = [Settings, Utilities](UObject* Object)
    {
        if (Settings.IsValid() && Settings->Graph == Object)
        {
            if (TSharedPtr<IPropertyUtilities> P = Utilities.Pin())
            {
#if WITH_DEV_AUTOMATION_TESTS
                ++RefreshNotificationsForTests;
#endif
                P->RequestForceRefresh();
            }
        }
    };
    PropertyChangedHandle = FCoreUObjectDelegates::OnObjectPropertyChanged.AddLambda([RefreshGraph](UObject* Object, FPropertyChangedEvent&) { RefreshGraph(Object); });
    TransactedHandle = FCoreUObjectDelegates::OnObjectTransacted.AddLambda([RefreshGraph](UObject* Object, const FTransactionObjectEvent&) { RefreshGraph(Object); });
    GraphHandle->SetOnPropertyValueChanged(FSimpleDelegate::CreateLambda([Utilities]() { if (TSharedPtr<IPropertyUtilities> P = Utilities.Pin()) { P->ForceRefresh(); } }));
    IDetailCategoryBuilder& Category = Builder.EditCategory(TEXT("Terrain Parameters"));
    if (!Settings->Graph)
    {
        Category.AddCustomRow(NSLOCTEXT("VoxelLand", "NoGraph", "Select a graph"))[SNew(STextBlock).Text(NSLOCTEXT("VoxelLand", "SelectGraph", "Select a terrain graph to edit parameter overrides."))];
        return;
    }
    for (const FVoxelLandGraphParameter& Parameter : Settings->Graph->Parameters)
    {
        const FGuid Id = Parameter.Id;
        const FString Name = Parameter.Name;
        FString Help = Parameter.Tooltip.ToString();
        if (Parameter.bHasRange && Parameter.Type == EVoxelLandGraphValueType::Float)
        {
            Help += FString::Printf(TEXT("\nRange: %.9g to %.9g"), double(Parameter.FloatMin), double(Parameter.FloatMax));
        }
        else if (Parameter.bHasRange && Parameter.Type == EVoxelLandGraphValueType::Int32)
        {
            Help += FString::Printf(TEXT("\nRange: %d to %d"), Parameter.IntMin, Parameter.IntMax);
        }
        Category.AddCustomRow(FText::FromString(Name))
            .NameContent()[SNew(STextBlock).Text(FText::FromString(Name)).ToolTipText(FText::FromString(Help))]
            .ValueContent().MinDesiredWidth(250.f)
            [ SNew(SVerticalBox)
              + SVerticalBox::Slot().AutoHeight()[ SNew(SHorizontalBox)
                + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[SNew(SCheckBox).ToolTipText(NSLOCTEXT("VoxelLand", "OverrideToggle", "Override this parameter"))
                    .IsChecked_Lambda([Settings, Id]() { return FindOverride(Settings.Get(), Id) ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
                    .OnCheckStateChanged_Lambda([Settings, Id, Overrides](ECheckBoxState State) { ToggleOverride(Settings, Id, Overrides, State == ECheckBoxState::Checked); })]
                + SHorizontalBox::Slot().FillWidth(1)[ValueWidget(Settings, Parameter, Overrides)] ]
              + SVerticalBox::Slot().AutoHeight()[SNew(STextBlock).ColorAndOpacity(FLinearColor::Red).Text_Lambda([Settings, Id]() { return Diagnostic(Settings, Id); })] ];
    }
    Category.AddCustomRow(NSLOCTEXT("VoxelLand", "OverrideDiagnostics", "Override diagnostics"))
        [SNew(STextBlock).AutoWrapText(true).ColorAndOpacity(FLinearColor::Red).Text_Lambda([Settings]() {
            if (!Settings.IsValid() || !Settings->Graph) { return FText::GetEmpty(); }
            TSet<FGuid> Seen;
            for (const FVoxelLandParameterOverride& O : Settings->Overrides)
            {
                if (!FindParameter(Settings.Get(), O.ParameterId) || Seen.Contains(O.ParameterId)) { return NSLOCTEXT("VoxelLand", "OrphanOverride", "An override is duplicated or refers to a removed parameter. Remove the extra entry in Overrides."); }
                Seen.Add(O.ParameterId);
            }
            return FText::GetEmpty();
        })];
}
