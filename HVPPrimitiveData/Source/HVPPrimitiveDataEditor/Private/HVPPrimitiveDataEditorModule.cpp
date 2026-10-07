#include "AssetRegistry/AssetRegistryModule.h"
#include "ContentBrowserMenuContexts.h"
#include "Containers/Ticker.h"
#include "EdGraphUtilities.h"
#include "InstanceDataLegend.h"
#include "InstanceDataLegendBinding.h"
#include "K2Node_SetNamedInstanceData.h"
#include "K2Node_SetNamedInstanceDataMulti.h"
#include "K2Node_SetNamedPrimitiveData.h"
#include "K2Node_SetNamedPrimitiveDataMulti.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Logging/MessageLog.h"
#include "Materials/Material.h"
#include "Materials/MaterialFunction.h"
#include "MessageLogModule.h"
#include "Modules/ModuleManager.h"
#include "PrimitiveDataLegend.h"
#include "PrimitiveDataLegendBinding.h"
#include "PropertyEditorModule.h"
#include "SetNamedInstanceDataMultiDetails.h"
#include "SetNamedPrimitiveDataMultiDetails.h"
#include "SGraphPinPrimitiveDataParameter.h"
#include "ScopedTransaction.h"
#include "ToolMenus.h"
#include "UObject/ObjectSaveContext.h"
#include "UObject/UObjectIterator.h"

#define LOCTEXT_NAMESPACE "HVPPrimitiveDataEditor"

namespace HVPPrimitiveDataEditor
{
	/** The binding that goes with each legend class, so the wiring below is written once for both. */
	template <typename TLegend> struct TBindingFor;
	template <> struct TBindingFor<UPrimitiveDataLegend> { using Type = FPrimitiveDataLegendBinding; };
	template <> struct TBindingFor<UInstanceDataLegend> { using Type = FInstanceDataLegendBinding; };
}

/**
 * Wiring, for both kinds of legend - Primitive Data and Instance Data. Three jobs, all reactions to
 * something the user did elsewhere:
 *
 *   - a legend was edited: re-lay out its bound materials, refresh the nodes that use it;
 *   - a bound material was saved: check it still matches its legend, and say so if not;
 *   - the right-click entries on legends and on materials.
 *
 * The first two are DEFERRED to the next tick. A legend edit arrives from inside PostEditChange (and
 * from inside undo), and a save arrives from inside SavePackage, where loading assets or recompiling
 * materials is at best rude and at worst illegal. One tick later nothing is mid-flight.
 */
class FHVPPrimitiveDataEditorModule : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		FMessageLogModule& MessageLog = FModuleManager::LoadModuleChecked<FMessageLogModule>("MessageLog");
		FMessageLogInitializationOptions Options;
		Options.bShowFilters = true;
		Options.bAllowClear = true;
		MessageLog.RegisterLogListing(FPrimitiveDataLegendBinding::LogName, LOCTEXT("LogLabel", "Primitive and Instance Data Legends"), Options);

		PinFactory = MakeShared<FPrimitiveDataParameterPinFactory>();
		FEdGraphUtilities::RegisterVisualPinFactory(PinFactory);

		FPropertyEditorModule& PropertyEditor = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");
		PropertyEditor.RegisterCustomClassLayout(UK2Node_SetNamedPrimitiveDataMulti::StaticClass()->GetFName(),
			FOnGetDetailCustomizationInstance::CreateStatic(&FSetNamedPrimitiveDataMultiDetails::MakeInstance));
		PropertyEditor.RegisterCustomClassLayout(UK2Node_SetNamedInstanceDataMulti::StaticClass()->GetFName(),
			FOnGetDetailCustomizationInstance::CreateStatic(&FSetNamedInstanceDataMultiDetails::MakeInstance));

		LegendChangedHandle = UPrimitiveDataLegend::OnChanged.AddRaw(this,
			&FHVPPrimitiveDataEditorModule::OnLegendChanged<UPrimitiveDataLegend>);
		InstanceLegendChangedHandle = UInstanceDataLegend::OnChanged.AddRaw(this,
			&FHVPPrimitiveDataEditorModule::OnLegendChanged<UInstanceDataLegend>);
		PreSaveHandle = FCoreUObjectDelegates::OnObjectPreSave.AddRaw(this, &FHVPPrimitiveDataEditorModule::OnObjectPreSave);

		UToolMenus::RegisterStartupCallback(FSimpleMulticastDelegate::FDelegate::CreateRaw(
			this, &FHVPPrimitiveDataEditorModule::RegisterMenus));
	}

	virtual void ShutdownModule() override
	{
		UToolMenus::UnRegisterStartupCallback(this);
		UToolMenus::UnregisterOwner(this);

		UPrimitiveDataLegend::OnChanged.Remove(LegendChangedHandle);
		UInstanceDataLegend::OnChanged.Remove(InstanceLegendChangedHandle);
		FCoreUObjectDelegates::OnObjectPreSave.Remove(PreSaveHandle);

		if (TickerHandle.IsValid())
		{
			FTSTicker::GetCoreTicker().RemoveTicker(TickerHandle);
			TickerHandle.Reset();
		}

		if (FPropertyEditorModule* PropertyEditor = FModuleManager::GetModulePtr<FPropertyEditorModule>("PropertyEditor"))
		{
			PropertyEditor->UnregisterCustomClassLayout(UK2Node_SetNamedPrimitiveDataMulti::StaticClass()->GetFName());
			PropertyEditor->UnregisterCustomClassLayout(UK2Node_SetNamedInstanceDataMulti::StaticClass()->GetFName());
		}

		if (PinFactory.IsValid())
		{
			FEdGraphUtilities::UnregisterVisualPinFactory(PinFactory);
			PinFactory.Reset();
		}

		if (FMessageLogModule* MessageLog = FModuleManager::GetModulePtr<FMessageLogModule>("MessageLog"))
		{
			MessageLog->UnregisterLogListing(FPrimitiveDataLegendBinding::LogName);
		}
	}

private:
	// -----------------------------------------------------------------------
	// Deferred work
	// -----------------------------------------------------------------------

	struct FPendingLegend
	{
		TMap<FName, FName> Renames;
		bool bLayoutChanged = false;
	};

	void Schedule()
	{
		if (!TickerHandle.IsValid())
		{
			TickerHandle = FTSTicker::GetCoreTicker().AddTicker(
				FTickerDelegate::CreateRaw(this, &FHVPPrimitiveDataEditorModule::RunDeferred));
		}
	}

	/** The pending edits for each legend class. */
	TMap<TWeakObjectPtr<UPrimitiveDataLegend>, FPendingLegend>& PendingFor(UPrimitiveDataLegend*) { return PendingLegends; }
	TMap<TWeakObjectPtr<UInstanceDataLegend>, FPendingLegend>& PendingFor(UInstanceDataLegend*) { return PendingInstanceLegends; }

	template <typename TLegend>
	void OnLegendChanged(TLegend* Legend, const TMap<FName, FName>& Renames, bool bLayoutChanged)
	{
		if (!Legend)
		{
			return;
		}

		FPendingLegend& Pending = PendingFor(Legend).FindOrAdd(Legend);
		Pending.bLayoutChanged |= bLayoutChanged;

		// Two renames of one entry inside a tick (A -> B, then B -> C) have to reach the material as
		// A -> C: it only ever had A.
		for (const TPair<FName, FName>& Rename : Renames)
		{
			bool bChained = false;
			for (TPair<FName, FName>& Existing : Pending.Renames)
			{
				if (Existing.Value == Rename.Key)
				{
					Existing.Value = Rename.Value;
					bChained = true;
				}
			}
			if (!bChained)
			{
				Pending.Renames.Add(Rename.Key, Rename.Value);
			}
		}

		Schedule();
	}

	void OnObjectPreSave(UObject* Object, FObjectPreSaveContext Context)
	{
		if (!Object || Context.IsProceduralSave() || IsRunningCommandlet())
		{
			return;
		}
		if (!Object->IsA<UMaterial>() && !Object->IsA<UMaterialFunction>())
		{
			return;
		}
		// The material editor's working copy lives in the transient package; only the real asset counts.
		if (Object->HasAnyFlags(RF_Transient) || Object->GetPackage()->HasAnyFlags(RF_Transient))
		{
			return;
		}

		PendingSaveChecks.Add(FSoftObjectPath(Object));
		Schedule();
	}

	bool RunDeferred(float)
	{
		TickerHandle.Reset();

		TSet<FSoftObjectPath> Saved = MoveTemp(PendingSaveChecks);
		PendingSaveChecks.Reset();

		RunPending<UPrimitiveDataLegend>();
		RunPending<UInstanceDataLegend>();

		CheckSaved(Saved);

		return false;	// one-shot; Schedule() adds a fresh ticker for the next batch
	}

	template <typename TLegend>
	void RunPending()
	{
		TMap<TWeakObjectPtr<TLegend>, FPendingLegend> Legends = MoveTemp(PendingFor(static_cast<TLegend*>(nullptr)));
		PendingFor(static_cast<TLegend*>(nullptr)).Reset();

		for (const TPair<TWeakObjectPtr<TLegend>, FPendingLegend>& Pair : Legends)
		{
			if (TLegend* Legend = Pair.Key.Get())
			{
				HVPPrimitiveDataEditor::TBindingFor<TLegend>::Type::SyncAll(*Legend, /*bApply*/ true, Pair.Value.Renames);
				if (Pair.Value.bLayoutChanged)
				{
					RefreshNodes(Legend);
				}
			}
		}
	}

	/**
	 * Reconstruct every loaded node that uses this legend and mark its Blueprint for recompiling.
	 *
	 * Reconstruction picks up renames and type changes on the pins. The recompile is what picks up a
	 * moved SLOT, which no pin shows: it lives only in the compiled code, as a literal. Unloaded
	 * Blueprints need nothing - they compile against the current legend when they load.
	 */
	static void RefreshNodes(UPrimitiveDataLegend* Legend)
	{
		RefreshNodes<UK2Node_SetNamedPrimitiveData, UK2Node_SetNamedPrimitiveDataMulti>(Legend);
	}

	static void RefreshNodes(UInstanceDataLegend* Legend)
	{
		RefreshNodes<UK2Node_SetNamedInstanceData, UK2Node_SetNamedInstanceDataMulti>(Legend);
	}

	template <typename TSingle, typename TMulti, typename TLegend>
	static void RefreshNodes(TLegend* Legend)
	{
		TSet<UBlueprint*> Touched;
		RefreshNodesOf<TSingle>(Legend, Touched);
		RefreshNodesOf<TMulti>(Legend, Touched, [](TMulti* Node)
		{
			// Its Details panel lists the legend's parameters; an edit may have added, removed or renamed some.
			Node->OnParametersOffered.Broadcast();
		});
		for (UBlueprint* Blueprint : Touched)
		{
			FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
		}
	}

	template <typename TNode, typename TLegend>
	static void RefreshNodesOf(TLegend* Legend, TSet<UBlueprint*>& Touched, TFunction<void(TNode*)> After = nullptr)
	{
		for (TObjectIterator<TNode> It; It; ++It)
		{
			TNode* Node = *It;
			if (!IsValid(Node) || Node->HasAnyFlags(RF_ClassDefaultObject | RF_ArchetypeObject) || Node->GetLegend() != Legend)
			{
				continue;
			}
			// No Blueprint means a compiler intermediate or an orphaned graph; nothing to refresh.
			UBlueprint* Blueprint = FBlueprintEditorUtils::FindBlueprintForNode(Node);
			if (!Blueprint)
			{
				continue;
			}
			Node->ReconstructNode();
			if (After)
			{
				After(Node);
			}
			Touched.Add(Blueprint);
		}
	}

	/**
	 * The "enforcing" half: a bound material that was just saved gets checked against its legend. It is
	 * reported, never changed - it has just been saved, and rewriting it would dirty it again.
	 */
	static void CheckSaved(const TSet<FSoftObjectPath>& Saved)
	{
		if (Saved.Num() == 0)
		{
			return;
		}

		FMessageLog Log(FPrimitiveDataLegendBinding::LogName);
		bool bProblems = false;
		for (const FSoftObjectPath& Path : Saved)
		{
			UObject* Material = Path.ResolveObject();
			if (!Material)
			{
				continue;
			}
			bProblems |= CheckSavedAgainst<UPrimitiveDataLegend>(Log, Material);
			bProblems |= CheckSavedAgainst<UInstanceDataLegend>(Log, Material);
		}
		if (bProblems)
		{
			Log.Notify(LOCTEXT("SavedDrift", "A saved material no longer matches its legend"),
				EMessageSeverity::Warning, /*bForce*/ true);
		}
	}

	template <typename TLegend>
	static bool CheckSavedAgainst(FMessageLog& Log, UObject* Material)
	{
		using FBinding = typename HVPPrimitiveDataEditor::TBindingFor<TLegend>::Type;
		bool bProblems = false;
		for (TLegend* Legend : FBinding::FindLegendsBinding(Material))
		{
			const FPrimitiveDataBindingResult Result = FBinding::Sync(*Legend, Material, /*bApply*/ false);
			if (!Result.IsClean())
			{
				FBinding::ReportTo(Log, *Legend, Material, Result);
				bProblems = true;
			}
		}
		return bProblems;
	}

	// -----------------------------------------------------------------------
	// Menus
	// -----------------------------------------------------------------------

	void RegisterMenus()
	{
		FToolMenuOwnerScoped OwnerScoped(this);

		RegisterLegendMenu<UPrimitiveDataLegend>(TEXT("HVPPrimitiveData"));
		RegisterLegendMenu<UInstanceDataLegend>(TEXT("HVPInstanceData"));
		RegisterBindMenu(UMaterial::StaticClass());
		// Material layers and blends derive from UMaterialFunction; their menus inherit this one.
		RegisterBindMenu(UMaterialFunction::StaticClass());
	}

	template <typename TLegend>
	static void RegisterLegendMenu(const TCHAR* Prefix)
	{
		UToolMenu* Menu = UE::ContentBrowser::ExtendToolMenu_AssetContextMenu(TLegend::StaticClass());
		if (!Menu)
		{
			return;
		}

		FToolMenuSection& Section = Menu->FindOrAddSection(TEXT("GetAssetActions"));
		Section.AddMenuEntry(
			FName(*FString::Printf(TEXT("%sSync"), Prefix)),
			LOCTEXT("SyncLabel", "Sync Bound Materials"),
			LOCTEXT("SyncTooltip",
				"Lay out every bound material by this legend: the parameters or nodes named like an entry are given "
				"its slot. Happens automatically when the legend is edited; this is for when a material changed instead."),
			FSlateIcon(),
			FToolUIActionChoice(FToolMenuExecuteAction::CreateStatic(&RunOnSelectedLegends<TLegend>, true)));

		Section.AddMenuEntry(
			FName(*FString::Printf(TEXT("%sCheck"), Prefix)),
			LOCTEXT("CheckLabel", "Check Bound Materials"),
			LOCTEXT("CheckTooltip", "Report how every bound material differs from this legend, without changing anything."),
			FSlateIcon(),
			FToolUIActionChoice(FToolMenuExecuteAction::CreateStatic(&RunOnSelectedLegends<TLegend>, false)));
	}

	template <typename TLegend>
	static void RunOnSelectedLegends(const FToolMenuContext& Context, bool bApply)
	{
		const UContentBrowserAssetContextMenuContext* Browser = Context.FindContext<UContentBrowserAssetContextMenuContext>();
		if (!Browser)
		{
			return;
		}
		for (TLegend* Legend : Browser->LoadSelectedObjects<TLegend>())
		{
			HVPPrimitiveDataEditor::TBindingFor<TLegend>::Type::SyncAll(*Legend, bApply);
		}
	}

	static void RegisterBindMenu(UClass* MaterialClass)
	{
		UToolMenu* Menu = UE::ContentBrowser::ExtendToolMenu_AssetContextMenu(MaterialClass);
		if (!Menu)
		{
			return;
		}

		FToolMenuSection& Section = Menu->FindOrAddSection(TEXT("GetAssetActions"));
		Section.AddSubMenu(
			TEXT("HVPPrimitiveDataBind"),
			LOCTEXT("BindLabel", "Bind to Primitive Data Legend"),
			LOCTEXT("BindTooltip",
				"Let a legend lay this material's custom primitive data out for it. A material belongs to one "
				"legend at a time; choosing the ticked one unbinds it."),
			FNewToolMenuDelegate::CreateStatic(&FillBindMenu<UPrimitiveDataLegend>));

		Section.AddSubMenu(
			TEXT("HVPInstanceDataBind"),
			LOCTEXT("BindInstanceLabel", "Bind to Instance Data Legend"),
			LOCTEXT("BindInstanceTooltip",
				"Let a legend lay out this material's per instance custom data: its PerInstanceCustomData nodes, "
				"named by their Description. A material belongs to one instance data legend at a time; choosing the "
				"ticked one unbinds it."),
			FNewToolMenuDelegate::CreateStatic(&FillBindMenu<UInstanceDataLegend>));
	}

	/**
	 * Built when the submenu opens, so it always lists the legends that exist now. Only the legends are
	 * loaded to read their binding lists - small assets. The selected materials are not loaded until
	 * one is actually bound.
	 */
	template <typename TLegend>
	static void FillBindMenu(UToolMenu* SubMenu)
	{
		using FBinding = typename HVPPrimitiveDataEditor::TBindingFor<TLegend>::Type;
		constexpr bool bInstance = std::is_same_v<TLegend, UInstanceDataLegend>;

		const UContentBrowserAssetContextMenuContext* Browser = SubMenu->FindContext<UContentBrowserAssetContextMenuContext>();
		if (!Browser)
		{
			return;
		}

		TArray<FSoftObjectPath> Selected;
		for (const FAssetData& Asset : Browser->SelectedAssets)
		{
			if (Asset.IsInstanceOf(UMaterial::StaticClass()) || Asset.IsInstanceOf(UMaterialFunction::StaticClass()))
			{
				Selected.Add(Asset.GetSoftObjectPath());
			}
		}

		TArray<FAssetData> Legends;
		FAssetRegistryModule::GetRegistry().GetAssetsByClass(TLegend::StaticClass()->GetClassPathName(), Legends);
		Legends.Sort([](const FAssetData& A, const FAssetData& B) { return A.AssetName.LexicalLess(B.AssetName); });

		FToolMenuSection& Section = SubMenu->AddSection(TEXT("Legends"), bInstance
			? LOCTEXT("InstanceLegendsHeading", "Instance Data Legends")
			: LOCTEXT("LegendsHeading", "Primitive Data Legends"));

		if (Legends.Num() == 0)
		{
			Section.AddMenuEntry(
				TEXT("NoLegends"),
				bInstance
					? LOCTEXT("NoInstanceLegendsLabel", "No legends yet - Add > Materials > Instance Data Legend")
					: LOCTEXT("NoLegendsLabel", "No legends yet - Add > Materials > Primitive Data Legend"),
				FText::GetEmpty(),
				FSlateIcon(),
				FUIAction(FExecuteAction(), FCanExecuteAction::CreateLambda([] { return false; })));
			return;
		}

		for (const FAssetData& LegendAsset : Legends)
		{
			const TLegend* Legend = Cast<TLegend>(LegendAsset.GetAsset());
			if (!Legend)
			{
				continue;
			}

			// Ticked when every selected material is already bound here; choosing it then unbinds.
			bool bAllBound = Selected.Num() > 0;
			for (const FSoftObjectPath& Path : Selected)
			{
				if (!Legend->BoundMaterials.ContainsByPredicate(
					[&Path](const TSoftObjectPtr<UObject>& Bound) { return Bound.ToSoftObjectPath() == Path; }))
				{
					bAllBound = false;
					break;
				}
			}

			const FSoftObjectPath LegendPath = LegendAsset.GetSoftObjectPath();
			Section.AddMenuEntry(
				LegendAsset.AssetName,
				FText::FromName(LegendAsset.AssetName),
				FText::Format(LOCTEXT("LegendEntryTooltip", "{0} parameter(s), {1} of {2} floats used."),
					FText::AsNumber(Legend->Parameters.Num()), FText::AsNumber(Legend->GetUsedFloats()),
					FText::AsNumber(TLegend::GetCapacity())),
				FSlateIcon(),
				FUIAction(
					FExecuteAction::CreateLambda([LegendPath, Selected, bAllBound]()
					{
						TLegend* Target = Cast<TLegend>(LegendPath.TryLoad());
						if (!Target)
						{
							return;
						}
						const FScopedTransaction Transaction(bAllBound
							? LOCTEXT("UnbindTransaction", "Unbind from Legend")
							: LOCTEXT("BindTransaction", "Bind to Legend"));
						for (const FSoftObjectPath& Path : Selected)
						{
							if (UObject* Material = Path.TryLoad())
							{
								if (bAllBound)
								{
									FBinding::Unbind(*Target, Material);
								}
								else
								{
									FBinding::Bind(*Target, Material);
								}
							}
						}
					}),
					FCanExecuteAction(),
					FIsActionChecked::CreateLambda([bAllBound]() { return bAllBound; })),
				EUserInterfaceActionType::ToggleButton);
		}
	}

	TSharedPtr<FPrimitiveDataParameterPinFactory> PinFactory;
	FDelegateHandle LegendChangedHandle;
	FDelegateHandle InstanceLegendChangedHandle;
	FDelegateHandle PreSaveHandle;
	FTSTicker::FDelegateHandle TickerHandle;

	TMap<TWeakObjectPtr<UPrimitiveDataLegend>, FPendingLegend> PendingLegends;
	TMap<TWeakObjectPtr<UInstanceDataLegend>, FPendingLegend> PendingInstanceLegends;
	TSet<FSoftObjectPath> PendingSaveChecks;
};

IMPLEMENT_MODULE(FHVPPrimitiveDataEditorModule, HVPPrimitiveDataEditor);

#undef LOCTEXT_NAMESPACE
