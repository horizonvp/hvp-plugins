#include "InstanceDataLegend.h"

#include "Misc/DataValidation.h"
#include "NamedDataLegendLayout.h"

#define LOCTEXT_NAMESPACE "InstanceDataLegend"

FOnInstanceDataLegendChanged UInstanceDataLegend::OnChanged;

const FInstanceDataLegendEntry* UInstanceDataLegend::FindParameter(const FGuid& Id) const
{
	if (!Id.IsValid())
	{
		return nullptr;
	}
	return Parameters.FindByPredicate([&Id](const FInstanceDataLegendEntry& E) { return E.Id == Id; });
}

const FInstanceDataLegendEntry* UInstanceDataLegend::FindParameter(FName Name) const
{
	if (Name.IsNone())
	{
		return nullptr;
	}
	return Parameters.FindByPredicate([Name](const FInstanceDataLegendEntry& E) { return E.Name == Name; });
}

int32 UInstanceDataLegend::GetUsedFloats() const
{
	return NamedDataLegendLayout::UsedFloats(Parameters);
}

void UInstanceDataLegend::PostInitProperties()
{
	Super::PostInitProperties();
	if (!HasAnyFlags(RF_ClassDefaultObject))
	{
		TakeSnapshot();
	}
}

void UInstanceDataLegend::PostLoad()
{
	Super::PostLoad();
	Normalize();
	TakeSnapshot();
}

bool UInstanceDataLegend::Normalize()
{
	bool bChanged = NamedDataLegendLayout::NormalizeIdentities(Parameters);
	bChanged |= NamedDataLegendLayout::AllocateSlots(Parameters, GetCapacity());

	// Transient and derived, so not a change in itself.
	FloatsPerInstance = 0;
	for (const FInstanceDataLegendEntry& Entry : Parameters)
	{
		if (Entry.HasSlot())
		{
			FloatsPerInstance = FMath::Max(FloatsPerInstance, Entry.Slot + Entry.GetWidth());
		}
	}
	return bChanged;
}

uint32 UInstanceDataLegend::ComputeLayoutHash() const
{
	return NamedDataLegendLayout::HashLayout(Parameters);
}

uint32 UInstanceDataLegend::ComputeBindingHash() const
{
	uint32 Hash = GetTypeHash(bRequireAllParameters);
	for (const TSoftObjectPtr<UObject>& Bound : BoundMaterials)
	{
		Hash = HashCombine(Hash, GetTypeHash(Bound.ToSoftObjectPath()));
	}
	return Hash;
}

void UInstanceDataLegend::TakeSnapshot()
{
	LastLayoutHash = ComputeLayoutHash();
	LastBindingHash = ComputeBindingHash();
	NamedDataLegendLayout::SnapshotNames(Parameters, LastNames);
}

#if WITH_EDITOR

void UInstanceDataLegend::PostEditChangeProperty(FPropertyChangedEvent& Event)
{
	Super::PostEditChangeProperty(Event);

	if (Event.ChangeType == EPropertyChangeType::Interactive)
	{
		return;
	}

	Normalize();

	const bool bLayoutChanged = ComputeLayoutHash() != LastLayoutHash;
	const bool bBindingChanged = ComputeBindingHash() != LastBindingHash;
	if (!bLayoutChanged && !bBindingChanged)
	{
		return;
	}

	const TMap<FName, FName> Renames = NamedDataLegendLayout::FindRenames(Parameters, LastNames);
	TakeSnapshot();
	OnChanged.Broadcast(this, Renames, bLayoutChanged);
}

EDataValidationResult UInstanceDataLegend::IsDataValid(FDataValidationContext& Context) const
{
	EDataValidationResult Result = Super::IsDataValid(Context);

	TSet<FName> Names;
	for (const FInstanceDataLegendEntry& Entry : Parameters)
	{
		if (Names.Contains(Entry.Name))
		{
			Context.AddError(FText::Format(
				LOCTEXT("DuplicateName", "'{0}' is named twice. Materials match nodes by name, so each must be unique."),
				FText::FromName(Entry.Name)));
			Result = EDataValidationResult::Invalid;
		}
		Names.Add(Entry.Name);

		if (!Entry.HasSlot())
		{
			Context.AddError(FText::Format(
				LOCTEXT("NoRoom", "'{0}' has no slot: a legend lays out at most {1} floats per instance and the other parameters use {2}. A vector needs three in a row."),
				FText::FromName(Entry.Name), FText::AsNumber(GetCapacity()), FText::AsNumber(GetUsedFloats())));
			Result = EDataValidationResult::Invalid;
		}
	}

	return Result;
}

#endif // WITH_EDITOR

#undef LOCTEXT_NAMESPACE
