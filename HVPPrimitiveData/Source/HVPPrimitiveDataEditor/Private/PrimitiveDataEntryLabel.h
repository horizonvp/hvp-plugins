#pragma once

#include "CoreMinimal.h"
#include "InstanceDataLegend.h"
#include "PrimitiveDataLegend.h"

/**
 * How a legend parameter is shown wherever one is picked: its name with its type and slot. The slot is
 * shown, not hidden - nobody has to look it up, but seeing it is useful when reading a material alongside.
 */
inline FText PrimitiveDataEntryLabel(const FPrimitiveDataLegendEntry& Entry)
{
	if (!Entry.HasSlot())
	{
		return FText::Format(NSLOCTEXT("SGraphPinPrimitiveDataParameter", "NoSlot", "{0}  (no slot - legend full)"),
			FText::FromName(Entry.Name));
	}
	if (Entry.Type == EPrimitiveDataParameterType::Vector)
	{
		return FText::Format(NSLOCTEXT("SGraphPinPrimitiveDataParameter", "VectorLabel", "{0}  (color, {1}-{2})"),
			FText::FromName(Entry.Name), FText::AsNumber(Entry.Slot), FText::AsNumber(Entry.Slot + 3));
	}
	return FText::Format(NSLOCTEXT("SGraphPinPrimitiveDataParameter", "ScalarLabel", "{0}  (scalar, {1})"),
		FText::FromName(Entry.Name), FText::AsNumber(Entry.Slot));
}

/** The same for an Instance Data Legend parameter: a vector there is three floats. */
inline FText PrimitiveDataEntryLabel(const FInstanceDataLegendEntry& Entry)
{
	if (!Entry.HasSlot())
	{
		return FText::Format(NSLOCTEXT("SGraphPinPrimitiveDataParameter", "NoSlot", "{0}  (no slot - legend full)"),
			FText::FromName(Entry.Name));
	}
	if (Entry.Type == EInstanceDataParameterType::Vector)
	{
		return FText::Format(NSLOCTEXT("SGraphPinPrimitiveDataParameter", "InstanceVectorLabel", "{0}  (RGB, {1}-{2})"),
			FText::FromName(Entry.Name), FText::AsNumber(Entry.Slot), FText::AsNumber(Entry.Slot + 2));
	}
	return FText::Format(NSLOCTEXT("SGraphPinPrimitiveDataParameter", "ScalarLabel", "{0}  (scalar, {1})"),
		FText::FromName(Entry.Name), FText::AsNumber(Entry.Slot));
}
