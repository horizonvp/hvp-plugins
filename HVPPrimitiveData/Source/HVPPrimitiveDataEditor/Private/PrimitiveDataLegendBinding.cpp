#include "PrimitiveDataLegendBinding.h"

#include "LegendBindingCommon.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionVectorParameter.h"
#include "Materials/MaterialFunction.h"
#include "PrimitiveDataLegend.h"

#define LOCTEXT_NAMESPACE "PrimitiveDataLegendBinding"

const FName FPrimitiveDataLegendBinding::LogName(TEXT("HVPPrimitiveData"));

namespace PrimitiveDataBinding
{
	FText TypeText(EPrimitiveDataParameterType Type)
	{
		return Type == EPrimitiveDataParameterType::Vector
			? LOCTEXT("Vector", "vector")
			: LOCTEXT("Scalar", "scalar");
	}

	/** A parameter node this binding manages, read into one shape so scalar and vector share a path. */
	struct FParameterNode
	{
		UMaterialExpression* Expression = nullptr;
		FName* Name = nullptr;
		bool* bUseCustomPrimitiveData = nullptr;
		uint8* Slot = nullptr;
		EPrimitiveDataParameterType Type = EPrimitiveDataParameterType::Scalar;
	};

	/**
	 * Exact classes only. Curve Atlas Row and Channel Mask parameters derive from Scalar and Vector
	 * Parameter and inherit the custom primitive data fields, but neither means anything read from
	 * primitive data, and a name clash with one should not quietly repurpose it.
	 */
	bool Read(UMaterialExpression* Expression, FParameterNode& Out)
	{
		if (!Expression)
		{
			return false;
		}
		if (Expression->GetClass() == UMaterialExpressionScalarParameter::StaticClass())
		{
			UMaterialExpressionScalarParameter* Scalar = CastChecked<UMaterialExpressionScalarParameter>(Expression);
			Out = { Expression, &Scalar->ParameterName, &Scalar->bUseCustomPrimitiveData, &Scalar->PrimitiveDataIndex,
				EPrimitiveDataParameterType::Scalar };
			return true;
		}
		if (Expression->GetClass() == UMaterialExpressionVectorParameter::StaticClass())
		{
			UMaterialExpressionVectorParameter* Vector = CastChecked<UMaterialExpressionVectorParameter>(Expression);
			Out = { Expression, &Vector->ParameterName, &Vector->bUseCustomPrimitiveData, &Vector->PrimitiveDataIndex,
				EPrimitiveDataParameterType::Vector };
			return true;
		}
		return false;
	}
}

FPrimitiveDataBindingResult FPrimitiveDataLegendBinding::Sync(const UPrimitiveDataLegend& Legend, UObject* MaterialOrFunction,
	bool bApply, const TMap<FName, FName>& Renames)
{
	using namespace PrimitiveDataBinding;

	FPrimitiveDataBindingResult Result;

	UMaterial* Material = Cast<UMaterial>(MaterialOrFunction);
	UMaterialFunction* Function = Cast<UMaterialFunction>(MaterialOrFunction);
	if (!Material && !Function)
	{
		Result.Errors.Add(LOCTEXT("NotAMaterial",
			"is not a material or material function. Material instances cannot be bound - the custom "
			"primitive data setting lives on the parent's parameter node, so bind the parent."));
		return Result;
	}

	const TConstArrayView<TObjectPtr<UMaterialExpression>> Expressions =
		Material ? Material->GetExpressions() : Function->GetExpressions();

	// Names already in the material. A rename only follows if the new name is free - otherwise the
	// material already has a parameter by that name, and relabelling a second one onto it would merge
	// two parameters that were separate.
	TSet<FName> NamesInMaterial;
	for (UMaterialExpression* Expression : Expressions)
	{
		FParameterNode Node;
		if (Read(Expression, Node))
		{
			NamesInMaterial.Add(*Node.Name);
		}
	}

	struct FChange
	{
		FParameterNode Node;
		FName NewName;
		uint8 NewSlot = 0;
	};
	TArray<FChange> Changes;
	TSet<FName> Found;

	for (UMaterialExpression* Expression : Expressions)
	{
		FParameterNode Node;
		if (!Read(Expression, Node))
		{
			continue;
		}

		FName Name = *Node.Name;
		if (const FName* Renamed = Renames.Find(Name))
		{
			if (!NamesInMaterial.Contains(*Renamed))
			{
				Name = *Renamed;
			}
		}

		const FPrimitiveDataLegendEntry* Entry = Legend.FindParameter(Name);
		if (!Entry)
		{
			// Not ours - an ordinary parameter, unless it reads primitive data, which is the one thing a
			// bound material must not do outside its legend.
			if (*Node.bUseCustomPrimitiveData)
			{
				Result.Errors.Add(FText::Format(
					LOCTEXT("Stray", "'{0}' reads custom primitive data (slot {1}), but the legend has no parameter by that name. "
						"Add it to the legend, or turn off Use Custom Primitive Data on the node."),
					FText::FromName(*Node.Name), FText::AsNumber(*Node.Slot)));
			}
			continue;
		}

		Found.Add(Entry->Name);

		if (Entry->Type != Node.Type)
		{
			Result.Errors.Add(FText::Format(
				LOCTEXT("WrongType", "'{0}' is a {1} parameter here but a {2} in the legend."),
				FText::FromName(Entry->Name), TypeText(Node.Type), TypeText(Entry->Type)));
			continue;
		}
		if (!Entry->HasSlot())
		{
			Result.Errors.Add(FText::Format(
				LOCTEXT("NoSlot", "'{0}' has no slot - the legend is out of room."), FText::FromName(Entry->Name)));
			continue;
		}

		const bool bSlotWrong = !*Node.bUseCustomPrimitiveData || *Node.Slot != Entry->Slot;
		const bool bNameWrong = *Node.Name != Entry->Name;
		if (bSlotWrong || bNameWrong)
		{
			Changes.Add({ Node, Entry->Name, static_cast<uint8>(Entry->Slot) });
		}
	}

	for (const FPrimitiveDataLegendEntry& Entry : Legend.Parameters)
	{
		if (Found.Contains(Entry.Name))
		{
			continue;
		}
		const FText Missing = FText::Format(
			LOCTEXT("Missing", "has no {0} parameter named '{1}'."), TypeText(Entry.Type), FText::FromName(Entry.Name));
		(Legend.bRequireAllParameters ? Result.Errors : Result.Warnings).Add(Missing);
	}

	if (Changes.Num() == 0)
	{
		return Result;
	}

	if (!bApply)
	{
		// A check, not a sync: say what a sync would do.
		for (const FChange& Change : Changes)
		{
			Result.Warnings.Add(FText::Format(
				LOCTEXT("OutOfStep", "'{0}' is not laid out on its slot ({1}) yet. Right-click the legend > Sync Bound Materials."),
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
		if (*Change.Node.Name != Change.NewName)
		{
			*Change.Node.Name = Change.NewName;
			++Result.ParametersRenamed;
		}
		if (!*Change.Node.bUseCustomPrimitiveData || *Change.Node.Slot != Change.NewSlot)
		{
			*Change.Node.bUseCustomPrimitiveData = true;
			*Change.Node.Slot = Change.NewSlot;
			++Result.ParametersWritten;
		}
	}

	LegendBindingCommon::Recompile(MaterialOrFunction);

	return Result;
}

void FPrimitiveDataLegendBinding::SyncAll(UPrimitiveDataLegend& Legend, bool bApply, const TMap<FName, FName>& Renames)
{
	LegendBindingCommon::SyncAll(Legend, bApply, Renames,
		[](const UPrimitiveDataLegend& L, UObject* Material, bool bApplyOne, const TMap<FName, FName>& R) { return Sync(L, Material, bApplyOne, R); });
}

TArray<UPrimitiveDataLegend*> FPrimitiveDataLegendBinding::FindLegendsBinding(const UObject* MaterialOrFunction)
{
	return LegendBindingCommon::FindLegendsBinding<UPrimitiveDataLegend>(MaterialOrFunction);
}

void FPrimitiveDataLegendBinding::Bind(UPrimitiveDataLegend& Legend, UObject* MaterialOrFunction)
{
	LegendBindingCommon::Bind(Legend, MaterialOrFunction, [](UPrimitiveDataLegend& L) { SyncAll(L, /*bApply*/ true); });
}

void FPrimitiveDataLegendBinding::Unbind(UPrimitiveDataLegend& Legend, UObject* MaterialOrFunction)
{
	LegendBindingCommon::Unbind(Legend, MaterialOrFunction);
}

void FPrimitiveDataLegendBinding::ReportTo(FMessageLog& Log, const UPrimitiveDataLegend& Legend, UObject* MaterialOrFunction,
	const FPrimitiveDataBindingResult& Result)
{
	LegendBindingCommon::Report(Log, Legend, MaterialOrFunction, Result);
}

#undef LOCTEXT_NAMESPACE
