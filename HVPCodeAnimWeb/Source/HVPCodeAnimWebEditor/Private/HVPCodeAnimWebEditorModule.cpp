#include "BlueprintEditor.h"
#include "BlueprintEditorContext.h"
#include "BlueprintEditorModule.h"
#include "CodeAnimationWeb.h"
#include "CodeAnimWebGraphs.h"
#include "CodeAnimOutputVariableDetails.h"
#include "CodeAnimWebEvents.h"
#include "CodeAnimWebStateDetails.h"
#include "Containers/Ticker.h"
#include "Editor.h"
#include "Engine/Blueprint.h"
#include "EdGraphUtilities.h"
#include "SGraphPinCodeAnimOutput.h"
#include "Engine/UserDefinedEnum.h"
#include "Kismet2/EnumEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Modules/ModuleManager.h"
#include "PropertyEditorModule.h"
#include "ToolMenus.h"
#include "UObject/UObjectIterator.h"
#include "WebGraph/SCodeAnimWebGraphEditor.h"
#include "Widgets/Docking/SDockTab.h"

#define LOCTEXT_NAMESPACE "HVPCodeAnimWebEditor"

/**
 * Re-syncs every loaded Web using a Blueprint enum when that enum is edited, so renaming, adding,
 * reordering or removing states shows up while the enum editor is still open rather than on the next compile.
 */
class FCodeAnimWebEnumListener : public FEnumEditorUtils::INotifyOnEnumChanged
{
public:
	virtual void PreChange(const UUserDefinedEnum* Changed, FEnumEditorUtils::EEnumEditorChangeInfo ChangedType) override {}

	virtual void PostChange(const UUserDefinedEnum* Changed, FEnumEditorUtils::EEnumEditorChangeInfo ChangedType) override
	{
		for (TObjectIterator<UClass> It; It; ++It)
		{
			UClass* Class = *It;
			if (!Class->IsChildOf(UCodeAnimationWeb::StaticClass())
				|| Class->HasAnyClassFlags(CLASS_Abstract | CLASS_NewerVersionExists)
				|| FKismetEditorUtilities::IsClassABlueprintSkeleton(Class))
			{
				continue;
			}
			UCodeAnimationWeb* Defaults = Cast<UCodeAnimationWeb>(Class->GetDefaultObject(/*bCreateIfNeeded*/ false));
			if (Defaults && Defaults->StateEnum == Changed)
			{
				Defaults->Modify();
				Defaults->SyncDefinition();
				// A state renamed in the enum: its graph's name follows.
				CodeAnimWebGraphs::RenameGraphsToMatch(Defaults->GetWebBlueprint());
			}
		}
	}
};

class FHVPCodeAnimWebEditorModule : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		FBlueprintEditorModule& Kismet = FModuleManager::LoadModuleChecked<FBlueprintEditorModule>("Kismet");
		VariableCustomizationHandle = Kismet.RegisterVariableCustomization(FProperty::StaticClass(),
			FOnGetVariableCustomizationInstance::CreateStatic(&FCodeAnimOutputVariableDetails::MakeInstance));

		FPropertyEditorModule& PropertyEditor = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");
		PropertyEditor.RegisterCustomPropertyTypeLayout(FCodeAnimWebStateEntry::StaticStruct()->GetFName(),
			FOnGetPropertyTypeCustomizationInstance::CreateStatic(&FCodeAnimWebStateEntryDetails::MakeInstance));
		PropertyEditor.RegisterCustomPropertyTypeLayout(FCodeAnimWebStateRef::StaticStruct()->GetFName(),
			FOnGetPropertyTypeCustomizationInstance::CreateStatic(&FCodeAnimWebStateRefDetails::MakeInstance));
		PropertyEditor.RegisterCustomPropertyTypeLayout(FCodeAnimWebTransition::StaticStruct()->GetFName(),
			FOnGetPropertyTypeCustomizationInstance::CreateStatic(&FCodeAnimWebTransitionDetails::MakeInstance));

		PropertyEditor.RegisterCustomClassLayout(UCodeAnimWebGraphSelection::StaticClass()->GetFName(),
			FOnGetDetailCustomizationInstance::CreateStatic(&FCodeAnimWebGraphSelectionDetails::MakeInstance));

		PinFactory = MakeShared<FCodeAnimOutputPinFactory>();
		FEdGraphUtilities::RegisterVisualPinFactory(PinFactory);

		EnumListener = MakeUnique<FCodeAnimWebEnumListener>();

		if (GEditor)
		{
			PreCompileHandle = GEditor->OnBlueprintPreCompile().AddRaw(this, &FHVPCodeAnimWebEditorModule::OnBlueprintPreCompile);
		}
		PropertyChangedHandle = FCoreUObjectDelegates::OnObjectPropertyChanged.AddRaw(this, &FHVPCodeAnimWebEditorModule::OnObjectPropertyChanged);

		UToolMenus::RegisterStartupCallback(FSimpleMulticastDelegate::FDelegate::CreateRaw(this, &FHVPCodeAnimWebEditorModule::RegisterMenus));
	}

	virtual void ShutdownModule() override
	{
		EnumListener.Reset();

		UToolMenus::UnRegisterStartupCallback(this);
		UToolMenus::UnregisterOwner(this);

		if (GEditor)
		{
			GEditor->OnBlueprintPreCompile().Remove(PreCompileHandle);
		}
		FCoreUObjectDelegates::OnObjectPropertyChanged.Remove(PropertyChangedHandle);
		if (TickerHandle.IsValid())
		{
			FTSTicker::GetCoreTicker().RemoveTicker(TickerHandle);
			TickerHandle.Reset();
		}

		if (PinFactory.IsValid())
		{
			FEdGraphUtilities::UnregisterVisualPinFactory(PinFactory);
			PinFactory.Reset();
		}

		if (FBlueprintEditorModule* Kismet = FModuleManager::GetModulePtr<FBlueprintEditorModule>("Kismet"))
		{
			Kismet->UnregisterVariableCustomization(FProperty::StaticClass(), VariableCustomizationHandle);
		}
		if (FPropertyEditorModule* PropertyEditor = FModuleManager::GetModulePtr<FPropertyEditorModule>("PropertyEditor"))
		{
			PropertyEditor->UnregisterCustomPropertyTypeLayout(FCodeAnimWebStateEntry::StaticStruct()->GetFName());
			PropertyEditor->UnregisterCustomPropertyTypeLayout(FCodeAnimWebStateRef::StaticStruct()->GetFName());
			PropertyEditor->UnregisterCustomPropertyTypeLayout(FCodeAnimWebTransition::StaticStruct()->GetFName());
			PropertyEditor->UnregisterCustomClassLayout(UCodeAnimWebGraphSelection::StaticClass()->GetFName());
		}
	}

private:
	/** A "Web Graph" button on the Blueprint editor's toolbar, for Webs only. */
	void RegisterMenus()
	{
		FToolMenuOwnerScoped Owner(this);
		UToolMenu* Toolbar = UToolMenus::Get()->ExtendMenu(TEXT("AssetEditor.BlueprintEditor.ToolBar"));
		FToolMenuSection& Section = Toolbar->FindOrAddSection(TEXT("CodeAnimationWeb"));
		Section.AddDynamicEntry(TEXT("OpenWebGraph"), FNewToolMenuSectionDelegate::CreateLambda([this](FToolMenuSection& InSection)
		{
			const UBlueprintEditorToolMenuContext* Context = InSection.FindContext<UBlueprintEditorToolMenuContext>();
			const UBlueprint* Blueprint = Context ? Context->GetBlueprintObj() : nullptr;
			const UClass* Parent = Blueprint ? Blueprint->ParentClass.Get() : nullptr;
			if (!Parent || !Parent->IsChildOf(UCodeAnimationWeb::StaticClass()))
			{
				return;
			}
			const TWeakPtr<FBlueprintEditor> Editor = Context->BlueprintEditor;
			InSection.AddEntry(FToolMenuEntry::InitToolBarButton(TEXT("OpenWebGraph"),
				FUIAction(FExecuteAction::CreateLambda([this, Editor]() { OpenWebGraph(Editor.Pin()); })),
				LOCTEXT("WebGraph", "Web Graph"),
				LOCTEXT("WebGraphTooltip", "The Web's states and transitions as a graph: drag between states to add transitions, "
					"double-click to open their graphs, select to edit them."),
				FSlateIcon(FAppStyle::GetAppStyleSetName(), TEXT("GraphEditor.StateMachine_16x"))));
		}));
	}

	/** Opens the Web Graph beside the Blueprint's other graphs, or brings it forward if it is open. */
	void OpenWebGraph(const TSharedPtr<FBlueprintEditor>& Editor)
	{
		UBlueprint* Blueprint = Editor.IsValid() ? Editor->GetBlueprintObj() : nullptr;
		if (!Blueprint)
		{
			return;
		}
		if (const TSharedPtr<SDockTab> Existing = WebGraphTabs.FindRef(Blueprint).Pin())
		{
			Existing->ActivateInParent(ETabActivationCause::SetDirectly);
			return;
		}

		const TSharedRef<SDockTab> Tab = SNew(SDockTab)
			.TabRole(ETabRole::DocumentTab)
			.Label(LOCTEXT("WebGraphTab", "Web Graph"))
			[
				SNew(SCodeAnimWebGraphEditor, Blueprint, Editor)
			];
		// "Document" is the closed placeholder every Blueprint editor layout keeps for its document tabs
		// (FDocumentTracker's default id) - graphs open there too. Any other id finds no placeholder, and
		// the tab is dropped without a word.
		Editor->GetTabManager()->InsertNewDocumentTab(TEXT("Document"), FTabManager::ESearchPreference::RequireClosedTab, Tab);
		Tab->ActivateInParent(ETabActivationCause::SetDirectly);
		WebGraphTabs.Add(Blueprint, Tab);
	}

	TMap<TWeakObjectPtr<UBlueprint>, TWeakPtr<SDockTab>> WebGraphTabs;

	/**
	 * A Web is compiling: after it finishes, check its outputs' dispatchers still match its outputs (a
	 * rename, a type change, an output added or removed), and if not, fix them and compile once more.
	 * Deferred to the next tick because a Blueprint must not be restructured in the middle of its compile.
	 */
	void OnBlueprintPreCompile(UBlueprint* Blueprint)
	{
		const UClass* Parent = Blueprint ? Blueprint->ParentClass.Get() : nullptr;
		if (IsRunningCommandlet() || GIsAutomationTesting || !Parent || !Parent->IsChildOf(UCodeAnimationWeb::StaticClass()))
		{
			return;
		}
		Schedule(Blueprint);
	}

	/**
	 * A Web's settings were edited - a transition row removed in the Details panel, say: delete any
	 * graph that edit left without an owner. Next tick, outside the edit, as its own undo step.
	 */
	void OnObjectPropertyChanged(UObject* Object, FPropertyChangedEvent& Event)
	{
		const UCodeAnimationWeb* Web = Cast<UCodeAnimationWeb>(Object);
		if (!Web || !Web->HasAnyFlags(RF_ClassDefaultObject) || IsRunningCommandlet() || GIsAutomationTesting)
		{
			return;
		}
		if (UBlueprint* Blueprint = Web->GetWebBlueprint())
		{
			Schedule(Blueprint);
		}
	}

	void Schedule(UBlueprint* Blueprint)
	{
		PendingWebs.Add(Blueprint);
		if (!TickerHandle.IsValid())
		{
			TickerHandle = FTSTicker::GetCoreTicker().AddTicker(
				FTickerDelegate::CreateRaw(this, &FHVPCodeAnimWebEditorModule::ReconcilePending));
		}
	}

	bool ReconcilePending(float)
	{
		TickerHandle.Reset();
		const TSet<TWeakObjectPtr<UBlueprint>> Batch = MoveTemp(PendingWebs);
		for (const TWeakObjectPtr<UBlueprint>& Weak : Batch)
		{
			UBlueprint* Blueprint = Weak.Get();
			if (!Blueprint)
			{
				continue;
			}
			// Graphs whose owner is gone (a transition deleted, an output taken off Custom, a variable
			// removed). Marks the Blueprint structurally modified itself if it removes any.
			CodeAnimWebGraphs::RemoveUnusedGraphs(Blueprint);
			// And names that no longer match: a transition's direction flipped in the Details panel, an
			// output renamed.
			CodeAnimWebGraphs::RenameGraphsToMatch(Blueprint);
			if (CodeAnimWebEvents::Reconcile(Blueprint))
			{
				FKismetEditorUtilities::CompileBlueprint(Blueprint);
			}
		}
		return false;
	}

	FDelegateHandle PreCompileHandle;
	FDelegateHandle PropertyChangedHandle;
	FTSTicker::FDelegateHandle TickerHandle;
	TSet<TWeakObjectPtr<UBlueprint>> PendingWebs;

	FDelegateHandle VariableCustomizationHandle;
	TSharedPtr<FCodeAnimOutputPinFactory> PinFactory;
	TUniquePtr<FCodeAnimWebEnumListener> EnumListener;
};

IMPLEMENT_MODULE(FHVPCodeAnimWebEditorModule, HVPCodeAnimWebEditor);

#undef LOCTEXT_NAMESPACE
