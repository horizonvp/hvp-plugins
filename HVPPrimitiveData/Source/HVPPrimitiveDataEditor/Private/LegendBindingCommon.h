#pragma once

#include "CoreMinimal.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Editor.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Logging/MessageLog.h"
#include "MaterialEditingLibrary.h"
#include "Materials/Material.h"
#include "Materials/MaterialFunction.h"
#include "Misc/UObjectToken.h"
#include "PrimitiveDataLegendBinding.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "Widgets/Notifications/SNotificationList.h"

#define LOCTEXT_NAMESPACE "PrimitiveDataLegendBinding"

/**
 * The half of binding that does not care which legend it is: reporting, syncing every bound material,
 * finding and editing binding lists, and the guard and recompile around writing to a material. Templates
 * over the legend class, which needs BoundMaterials and GetName(); the material-specific half - which
 * nodes a legend owns and how - is each binding's Sync.
 */
namespace LegendBindingCommon
{
	template <typename TLegend>
	void Report(FMessageLog& Log, const TLegend& Legend, UObject* Material, const FPrimitiveDataBindingResult& Result)
	{
		for (const FText& Error : Result.Errors)
		{
			Log.Error()
				->AddToken(FUObjectToken::Create(Material))
				->AddToken(FTextToken::Create(Error))
				->AddToken(FTextToken::Create(LOCTEXT("Via", "  (legend:")))
				->AddToken(FUObjectToken::Create(&Legend))
				->AddToken(FTextToken::Create(LOCTEXT("Close", ")")));
		}
		for (const FText& Warning : Result.Warnings)
		{
			Log.Warning()
				->AddToken(FUObjectToken::Create(Material))
				->AddToken(FTextToken::Create(Warning))
				->AddToken(FTextToken::Create(LOCTEXT("Via", "  (legend:")))
				->AddToken(FUObjectToken::Create(&Legend))
				->AddToken(FTextToken::Create(LOCTEXT("Close", ")")));
		}
		if (!Result.SkippedReason.IsEmpty())
		{
			Log.Warning()
				->AddToken(FUObjectToken::Create(Material))
				->AddToken(FTextToken::Create(Result.SkippedReason));
		}
	}

	/** Sync every bound material with SyncOne(Legend, Material, bApply, Renames), reporting to the message log. */
	template <typename TLegend, typename TSyncOne>
	void SyncAll(TLegend& Legend, bool bApply, const TMap<FName, FName>& Renames, TSyncOne SyncOne)
	{
		FMessageLog Log(FPrimitiveDataLegendBinding::LogName);
		bool bProblems = false;
		int32 Written = 0;
		int32 Renamed = 0;
		int32 Checked = 0;

		for (const TSoftObjectPtr<UObject>& Bound : Legend.BoundMaterials)
		{
			if (Bound.IsNull())
			{
				continue;
			}

			UObject* Material = Bound.LoadSynchronous();
			if (!Material)
			{
				Log.Warning()
					->AddToken(FUObjectToken::Create(&Legend))
					->AddToken(FTextToken::Create(FText::Format(
						LOCTEXT("Unloadable", "binds {0}, which could not be loaded. Remove it from Bound Materials if it was deleted."),
						FText::FromString(Bound.ToString()))));
				bProblems = true;
				continue;
			}

			++Checked;
			const FPrimitiveDataBindingResult Result = SyncOne(Legend, Material, bApply, Renames);
			Written += Result.ParametersWritten;
			Renamed += Result.ParametersRenamed;
			if (!Result.IsClean())
			{
				Report(Log, Legend, Material, Result);
				bProblems = true;
			}
		}

		if (bProblems)
		{
			Log.Notify(FText::Format(LOCTEXT("Problems", "{0}: some bound materials need attention"),
				FText::FromString(Legend.GetName())), EMessageSeverity::Warning, /*bForce*/ true);
			return;
		}

		// Clean. Say so only when something actually happened - a no-op sync on every legend edit would
		// otherwise put a toast up for typing a name.
		if (Written > 0 || Renamed > 0 || !bApply)
		{
			FNotificationInfo Info(bApply
				? FText::Format(LOCTEXT("Synced", "{0}: laid out {1} parameter(s), renamed {2}, across {3} material(s)"),
					FText::FromString(Legend.GetName()), FText::AsNumber(Written), FText::AsNumber(Renamed), FText::AsNumber(Checked))
				: FText::Format(LOCTEXT("Checked", "{0}: all {1} bound material(s) match"),
					FText::FromString(Legend.GetName()), FText::AsNumber(Checked)));
			Info.ExpireDuration = 4.f;
			FSlateNotificationManager::Get().AddNotification(Info);
		}
	}

	template <typename TLegend>
	bool IsBound(const TLegend& Legend, const FSoftObjectPath& Path)
	{
		return Legend.BoundMaterials.ContainsByPredicate(
			[&Path](const TSoftObjectPtr<UObject>& Bound) { return Bound.ToSoftObjectPath() == Path; });
	}

	/** Every legend of this class in the project that binds this material or function. */
	template <typename TLegend>
	TArray<TLegend*> FindLegendsBinding(const UObject* MaterialOrFunction)
	{
		TArray<TLegend*> Out;
		if (!MaterialOrFunction)
		{
			return Out;
		}

		const FSoftObjectPath Path(MaterialOrFunction);
		TArray<FAssetData> Assets;
		FAssetRegistryModule::GetRegistry().GetAssetsByClass(TLegend::StaticClass()->GetClassPathName(), Assets);

		// Legends are a handful of small assets, so loading them to read the binding list is cheaper than
		// maintaining a searchable tag for it.
		for (const FAssetData& Asset : Assets)
		{
			TLegend* Legend = Cast<TLegend>(Asset.GetAsset());
			if (Legend && IsBound(*Legend, Path))
			{
				Out.Add(Legend);
			}
		}
		return Out;
	}

	template <typename TLegend>
	void Unbind(TLegend& Legend, UObject* MaterialOrFunction)
	{
		const FSoftObjectPath Path(MaterialOrFunction);
		Legend.Modify();
		const int32 Removed = Legend.BoundMaterials.RemoveAll(
			[&Path](const TSoftObjectPtr<UObject>& Bound) { return Bound.ToSoftObjectPath() == Path; });
		if (Removed > 0)
		{
			// The material keeps its layout. Unbinding stops the legend managing it; it does not undo
			// what the legend wrote, which would change how the material renders.
			Legend.PostEditChange();
		}
	}

	/**
	 * Bind to exactly this legend: removes the material from any other legend of the same class first, as
	 * two legends writing slots into one material would fight. Then syncs it, via SyncAllFn when already
	 * bound and through the legend's own change notification otherwise.
	 */
	template <typename TLegend, typename TSyncAll>
	void Bind(TLegend& Legend, UObject* MaterialOrFunction, TSyncAll SyncAllFn)
	{
		if (!MaterialOrFunction)
		{
			return;
		}

		for (TLegend* Other : FindLegendsBinding<TLegend>(MaterialOrFunction))
		{
			if (Other != &Legend)
			{
				Unbind(*Other, MaterialOrFunction);
			}
		}

		if (IsBound(Legend, FSoftObjectPath(MaterialOrFunction)))
		{
			SyncAllFn(Legend);
			return;
		}

		Legend.Modify();
		Legend.BoundMaterials.Add(TSoftObjectPtr<UObject>(MaterialOrFunction));
		// Through PostEditChange, not a direct sync: the legend notices its own change, re-snapshots, and
		// its listener syncs - the same path a details-panel edit takes, so there is only one.
		Legend.PostEditChange();
	}

	/**
	 * The material editor edits a COPY and writes it back over the original on Apply. Changing the
	 * original underneath it would be silently undone the next time anyone presses Apply there. True if
	 * it is safe to write; otherwise the result says why not.
	 */
	inline bool CanWrite(UObject* MaterialOrFunction, int32 PendingChanges, FPrimitiveDataBindingResult& Result)
	{
		if (GEditor)
		{
			if (UAssetEditorSubsystem* Editors = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>())
			{
				if (Editors->FindEditorForAsset(MaterialOrFunction, /*bFocusIfOpen*/ false))
				{
					Result.SkippedReason = FText::Format(
						LOCTEXT("OpenInEditor", "is open in the material editor, which works on a copy and would overwrite "
							"these changes on Apply. {0} parameter(s) still need laying out - close it and sync again."),
						FText::AsNumber(PendingChanges));
					return false;
				}
			}
		}
		return true;
	}

	/** Recompile after nodes were rewritten, and mark the asset for saving. */
	inline void Recompile(UObject* MaterialOrFunction)
	{
		if (UMaterial* Material = Cast<UMaterial>(MaterialOrFunction))
		{
			UMaterialEditingLibrary::RecompileMaterial(Material);
		}
		else if (UMaterialFunction* Function = Cast<UMaterialFunction>(MaterialOrFunction))
		{
			UMaterialEditingLibrary::UpdateMaterialFunction(Function, nullptr);
		}
		MaterialOrFunction->MarkPackageDirty();
	}
}

#undef LOCTEXT_NAMESPACE
