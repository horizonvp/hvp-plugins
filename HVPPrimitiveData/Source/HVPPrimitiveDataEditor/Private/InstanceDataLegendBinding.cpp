#include "InstanceDataLegendBinding.h"

#include "InstanceDataLegend.h"
#include "LegendBindingCommon.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionPerInstanceCustomData.h"
#include "Materials/MaterialFunction.h"

#define LOCTEXT_NAMESPACE "InstanceDataLegendBinding"

namespace InstanceDataBinding
{
	FText TypeText(EInstanceDataParameterType Type)
	{
		return Type == EInstanceDataParameterType::Vector
			? LOCTEXT("Vector", "PerInstanceCustomData3Vector")
			: LOCTEXT("Scalar", "PerInstanceCustomData");
	}

	/**
	 * The engine does not export these expression classes, so their StaticClass() cannot be linked to from
	 * here. Their properties are plain public members, though, and the classes are found by path.
	 */
	const UClass* ScalarClass()
	{
		static const TWeakObjectPtr<UClass> Class = FindObject<UClass>(nullptr, TEXT("/Script/Engine.MaterialExpressionPerInstanceCustomData"));
		return Class.Get();
	}

	const UClass* VectorClass()
	{
		static const TWeakObjectPtr<UClass> Class = FindObject<UClass>(nullptr, TEXT("/Script/Engine.MaterialExpressionPerInstanceCustomData3Vector"));
		return Class.Get();
	}

	/** A per instance custom data node, read into one shape so scalar and vector share a path. */
	struct FDataNode
	{
		UMaterialExpression* Expression = nullptr;
		/** The Description, trimmed: the node's name as far as the legend is concerned. None if blank. */
		FName Name;
		uint32* Slot = nullptr;
		EInstanceDataParameterType Type = EInstanceDataParameterType::Scalar;
	};

	/** Exact classes only, as for primitive data: nothing deriving from them is ours to rewrite. */
	bool Read(UMaterialExpression* Expression, FDataNode& Out)
	{
		if (!Expression)
		{
			return false;
		}
		const UClass* Class = Expression->GetClass();
		uint32* Slot = nullptr;
		EInstanceDataParameterType Type = EInstanceDataParameterType::Scalar;
		if (Class == ScalarClass())
		{
			Slot = &static_cast<UMaterialExpressionPerInstanceCustomData*>(Expression)->DataIndex;
		}
		else if (Class == VectorClass())
		{
			Slot = &static_cast<UMaterialExpressionPerInstanceCustomData3Vector*>(Expression)->DataIndex;
			Type = EInstanceDataParameterType::Vector;
		}
		else
		{
			return false;
		}
		const FString Description = Expression->Desc.TrimStartAndEnd();
		Out = { Expression, Description.IsEmpty() ? NAME_None : FName(*Description), Slot, Type };
		return true;
	}
}

FPrimitiveDataBindingResult FInstanceDataLegendBinding::Sync(const UInstanceDataLegend& Legend, UObject* MaterialOrFunction,
	bool bApply, const TMap<FName, FName>& Renames)
{
	using namespace InstanceDataBinding;

	FPrimitiveDataBindingResult Result;

	UMaterial* Material = Cast<UMaterial>(MaterialOrFunction);
	UMaterialFunction* Function = Cast<UMaterialFunction>(MaterialOrFunction);
	if (!Material && !Function)
	{
		Result.Errors.Add(LOCTEXT("NotAMaterial",
			"is not a material or material function. Material instances cannot be bound - the per instance "
			"custom data nodes live in the parent, so bind the parent."));
		return Result;
	}

	const TConstArrayView<TObjectPtr<UMaterialExpression>> Expressions =
		Material ? Material->GetExpressions() : Function->GetExpressions();

	// Names already in the material. A rename only follows if the new name is free, or two parameters
	// that were separate would be merged.
	TSet<FName> NamesInMaterial;
	for (UMaterialExpression* Expression : Expressions)
	{
		FDataNode Node;
		if (Read(Expression, Node) && !Node.Name.IsNone())
		{
			NamesInMaterial.Add(Node.Name);
		}
	}

	struct FChange
	{
		FDataNode Node;
		FName NewName;
		uint32 NewSlot = 0;
	};
	TArray<FChange> Changes;
	TSet<FName> Found;

	for (UMaterialExpression* Expression : Expressions)
	{
		FDataNode Node;
		if (!Read(Expression, Node))
		{
			continue;
		}

		// Every per instance custom data node in a bound material belongs to the legend - there is no
		// "ordinary" one, as there is an ordinary parameter. So an unnamed one is an error, not ignored.
		if (Node.Name.IsNone())
		{
			Result.Errors.Add(FText::Format(
				LOCTEXT("Unnamed", "has a {0} node reading float {1} with no Description, so the legend cannot tell which "
					"parameter it is. Set its Description to one of the legend's parameter names."),
				TypeText(Node.Type), FText::AsNumber(*Node.Slot)));
			continue;
		}

		FName Name = Node.Name;
		if (const FName* Renamed = Renames.Find(Name))
		{
			if (!NamesInMaterial.Contains(*Renamed))
			{
				Name = *Renamed;
			}
		}

		const FInstanceDataLegendEntry* Entry = Legend.FindParameter(Name);
		if (!Entry)
		{
			Result.Errors.Add(FText::Format(
				LOCTEXT("Stray", "'{0}' reads per instance custom data (float {1}), but the legend has no parameter by that name. "
					"Add it to the legend, or correct the node's Description."),
				FText::FromName(Node.Name), FText::AsNumber(*Node.Slot)));
			continue;
		}

		Found.Add(Entry->Name);

		if (Entry->Type != Node.Type)
		{
			Result.Errors.Add(FText::Format(
				LOCTEXT("WrongType", "'{0}' is read with a {1} node here, but the legend needs a {2}."),
				FText::FromName(Entry->Name), TypeText(Node.Type), TypeText(Entry->Type)));
			continue;
		}
		if (!Entry->HasSlot())
		{
			Result.Errors.Add(FText::Format(
				LOCTEXT("NoSlot", "'{0}' has no slot - the legend is out of room."), FText::FromName(Entry->Name)));
			continue;
		}

		if (*Node.Slot != static_cast<uint32>(Entry->Slot) || Node.Name != Entry->Name)
		{
			Changes.Add({ Node, Entry->Name, static_cast<uint32>(Entry->Slot) });
		}
	}

	for (const FInstanceDataLegendEntry& Entry : Legend.Parameters)
	{
		if (Found.Contains(Entry.Name))
		{
			continue;
		}
		const FText Missing = FText::Format(
			LOCTEXT("Missing", "has no {0} node described '{1}'."), TypeText(Entry.Type), FText::FromName(Entry.Name));
		(Legend.bRequireAllParameters ? Result.Errors : Result.Warnings).Add(Missing);
	}

	if (Changes.Num() == 0)
	{
		return Result;
	}

	if (!bApply)
	{
		for (const FChange& Change : Changes)
		{
			Result.Warnings.Add(FText::Format(
				LOCTEXT("OutOfStep", "'{0}' is not reading its slot ({1}) yet. Right-click the legend > Sync Bound Materials."),
				FText::FromName(Change.NewName), FText::AsNumber(Change.NewSlot)));
		}
		return Result;
	}

	if (!LegendBindingCommon::CanWrite(MaterialOrFunction, Changes.Num(), Result))
	{
		return Result;
	}

	MaterialOrFunction->Modify();
	for (const FChange& Change : Changes)
	{
		Change.Node.Expression->Modify();
		if (Change.Node.Name != Change.NewName)
		{
			Change.Node.Expression->Desc = Change.NewName.ToString();
			++Result.ParametersRenamed;
		}
		if (*Change.Node.Slot != Change.NewSlot)
		{
			*Change.Node.Slot = Change.NewSlot;
			++Result.ParametersWritten;
		}
	}

	LegendBindingCommon::Recompile(MaterialOrFunction);

	return Result;
}

void FInstanceDataLegendBinding::SyncAll(UInstanceDataLegend& Legend, bool bApply, const TMap<FName, FName>& Renames)
{
	LegendBindingCommon::SyncAll(Legend, bApply, Renames,
		[](const UInstanceDataLegend& L, UObject* Material, bool bApplyOne, const TMap<FName, FName>& R) { return Sync(L, Material, bApplyOne, R); });
}

TArray<UInstanceDataLegend*> FInstanceDataLegendBinding::FindLegendsBinding(const UObject* MaterialOrFunction)
{
	return LegendBindingCommon::FindLegendsBinding<UInstanceDataLegend>(MaterialOrFunction);
}

void FInstanceDataLegendBinding::Bind(UInstanceDataLegend& Legend, UObject* MaterialOrFunction)
{
	LegendBindingCommon::Bind(Legend, MaterialOrFunction, [](UInstanceDataLegend& L) { SyncAll(L, /*bApply*/ true); });
}

void FInstanceDataLegendBinding::Unbind(UInstanceDataLegend& Legend, UObject* MaterialOrFunction)
{
	LegendBindingCommon::Unbind(Legend, MaterialOrFunction);
}

void FInstanceDataLegendBinding::ReportTo(FMessageLog& Log, const UInstanceDataLegend& Legend, UObject* MaterialOrFunction,
	const FPrimitiveDataBindingResult& Result)
{
	LegendBindingCommon::Report(Log, Legend, MaterialOrFunction, Result);
}

#undef LOCTEXT_NAMESPACE
