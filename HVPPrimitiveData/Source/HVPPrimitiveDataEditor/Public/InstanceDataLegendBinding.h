#pragma once

#include "CoreMinimal.h"
#include "PrimitiveDataLegendBinding.h"

class UInstanceDataLegend;

/**
 * Lays bound materials out by an Instance Data Legend - the Primitive Data Legend binding's twin.
 *
 * Materials read per instance custom data through PerInstanceCustomData (one float) and
 * PerInstanceCustomData3Vector (three) nodes, which have a Data Index but no parameter name. So the name
 * is the node's Description - the comment shown above it in the graph: a node described exactly like an
 * entry is that entry, and binding writes the entry's slot into its Data Index. Several nodes may carry
 * the same name; they all read the same parameter.
 *
 * As with primitive data, it never guesses: a bound material's per instance custom data node with no
 * Description, or one naming nothing in the legend, is reported and left alone.
 */
struct HVPPRIMITIVEDATAEDITOR_API FInstanceDataLegendBinding
{
	/** Check one material or function against the legend, and with bApply write what is needed. Idempotent. */
	static FPrimitiveDataBindingResult Sync(const UInstanceDataLegend& Legend, UObject* MaterialOrFunction,
		bool bApply, const TMap<FName, FName>& Renames = TMap<FName, FName>());

	/** Sync every bound material, reporting to the Primitive Data Legend message log. */
	static void SyncAll(UInstanceDataLegend& Legend, bool bApply,
		const TMap<FName, FName>& Renames = TMap<FName, FName>());

	/** Every instance data legend in the project that binds this material or function. */
	static TArray<UInstanceDataLegend*> FindLegendsBinding(const UObject* MaterialOrFunction);

	/** Bind to exactly this instance data legend, unbinding it from any other first. A primitive data legend binding is untouched. */
	static void Bind(UInstanceDataLegend& Legend, UObject* MaterialOrFunction);
	static void Unbind(UInstanceDataLegend& Legend, UObject* MaterialOrFunction);

	static void ReportTo(class FMessageLog& Log, const UInstanceDataLegend& Legend, UObject* MaterialOrFunction,
		const FPrimitiveDataBindingResult& Result);
};
