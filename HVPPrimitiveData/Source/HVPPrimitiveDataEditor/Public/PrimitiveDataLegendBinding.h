#pragma once

#include "CoreMinimal.h"

class UObject;
class UPrimitiveDataLegend;

/** What syncing one material against its legend found, and did. */
struct FPrimitiveDataBindingResult
{
	TArray<FText> Errors;
	TArray<FText> Warnings;

	/** Parameters whose custom primitive data setting or slot was written. */
	int32 ParametersWritten = 0;

	/** Parameters relabelled to follow a rename in the legend. */
	int32 ParametersRenamed = 0;

	/** Changes were needed but not made, and why. */
	FText SkippedReason;

	bool IsClean() const { return Errors.Num() == 0 && Warnings.Num() == 0 && SkippedReason.IsEmpty(); }
};

/**
 * Lays bound materials out by a legend.
 *
 * The legend is the source of truth and binding WRITES to the material: a Scalar or Vector Parameter
 * named like an entry is switched to custom primitive data and given that entry's slot. So nobody types
 * a slot number, and nothing has to be kept in step by hand.
 *
 * What it will not do is guess. A parameter reading custom primitive data the legend does not name is
 * reported, never switched off - that might be deliberate, and silently changing a material's
 * behaviour is worse than a message saying it looks wrong.
 */
struct HVPPRIMITIVEDATAEDITOR_API FPrimitiveDataLegendBinding
{
	/**
	 * Check one material or material function against a legend, and with bApply write whatever is
	 * needed. Idempotent: a material already laid out is neither modified nor recompiled.
	 *
	 * Renames maps old -> new parameter names from the edit that triggered this, so a relabelled
	 * parameter follows rather than being reported missing-and-stray.
	 */
	static FPrimitiveDataBindingResult Sync(const UPrimitiveDataLegend& Legend, UObject* MaterialOrFunction,
		bool bApply, const TMap<FName, FName>& Renames = TMap<FName, FName>());

	/** Sync every bound material, reporting to the message log. Opens the log only if there is something to see. */
	static void SyncAll(UPrimitiveDataLegend& Legend, bool bApply,
		const TMap<FName, FName>& Renames = TMap<FName, FName>());

	/** Every legend in the project that binds this material or function. */
	static TArray<UPrimitiveDataLegend*> FindLegendsBinding(const UObject* MaterialOrFunction);

	/**
	 * Bind to exactly this legend: removes the material from any other legend first, as two legends
	 * writing slots into one material would fight. Then syncs it.
	 */
	static void Bind(UPrimitiveDataLegend& Legend, UObject* MaterialOrFunction);
	static void Unbind(UPrimitiveDataLegend& Legend, UObject* MaterialOrFunction);

	/** Write one material's result to a log as clickable lines. */
	static void ReportTo(class FMessageLog& Log, const UPrimitiveDataLegend& Legend, UObject* MaterialOrFunction,
		const FPrimitiveDataBindingResult& Result);

	/** Message log listing everything here reports to. */
	static const FName LogName;
};
