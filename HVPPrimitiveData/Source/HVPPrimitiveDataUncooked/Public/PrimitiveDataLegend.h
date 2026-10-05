#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "UObject/SoftObjectPtr.h"

#include "PrimitiveDataLegend.generated.h"

class UPrimitiveDataLegend;

UENUM()
enum class EPrimitiveDataParameterType : uint8
{
	/** One float. Matches a Scalar Parameter. */
	Scalar	UMETA(DisplayName = "Scalar"),
	/** Four consecutive floats. Matches a Vector Parameter; set from Blueprint as a Linear Color. */
	Vector	UMETA(DisplayName = "Vector (Color)"),
};

/** One named parameter and the custom primitive data slot it owns. */
USTRUCT()
struct HVPPRIMITIVEDATAUNCOOKED_API FPrimitiveDataLegendEntry
{
	GENERATED_BODY()

	/**
	 * The label, and the link to materials: a bound material's Scalar or Vector Parameter with this
	 * exact name is the one that gets this slot.
	 */
	UPROPERTY(EditAnywhere, Category = "Parameter")
	FName Name;

	UPROPERTY(EditAnywhere, Category = "Parameter")
	EPrimitiveDataParameterType Type = EPrimitiveDataParameterType::Scalar;

	/**
	 * First custom primitive data float this parameter occupies; a vector takes this one and the next
	 * three. Assigned for you, and STABLE - adding, removing or reordering other entries never moves
	 * it. Only this entry changing type to something that no longer fits where it is moves it.
	 */
	UPROPERTY(VisibleAnywhere, Category = "Parameter")
	int32 Slot = INDEX_NONE;

	/**
	 * Identity that survives a rename. Blueprint nodes hold this rather than the name, so relabelling
	 * a parameter here never orphans a node that uses it.
	 */
	UPROPERTY()
	FGuid Id;

	/** The width Slot was allocated for. Lets the allocator tell "this entry changed" from "a neighbour changed". */
	UPROPERTY()
	int32 AllocatedWidth = 0;

	int32 GetWidth() const { return Type == EPrimitiveDataParameterType::Vector ? 4 : 1; }
	bool HasSlot() const { return Slot != INDEX_NONE; }
};

/**
 * Legend; old name -> new name for every entry relabelled by the edit; and whether the parameter
 * LAYOUT changed (names, types, slots) as opposed to only the binding list. Blueprints only need
 * recompiling for the former, and recompiling marks them dirty.
 */
// Aliased because the comma inside TMap<FName, FName> would split the macro's arguments.
using FPrimitiveDataRenames = TMap<FName, FName>;
DECLARE_MULTICAST_DELEGATE_ThreeParams(FOnPrimitiveDataLegendChanged, UPrimitiveDataLegend*, const FPrimitiveDataRenames&, bool);

/**
 * A named layout for a primitive's custom primitive data: which parameter lives in which float.
 *
 * The point is that nobody types a slot number. Parameters are labelled here and slots are allocated
 * for them; bound materials have those slots written into their parameters; Set Named Primitive Data
 * picks parameters by name and bakes the slot in when the Blueprint compiles.
 *
 * EDITOR-ONLY. Nothing reads a legend at runtime - by the time a Blueprint runs, the slot is a literal
 * in an engine call - so the cooker leaves these assets out.
 */
UCLASS(meta = (DisplayName = "Primitive Data Legend"))
class HVPPRIMITIVEDATAUNCOOKED_API UPrimitiveDataLegend : public UObject
{
	GENERATED_BODY()

public:
	/** The parameters. Every primitive has 36 floats of custom data: a scalar costs one, a vector four. */
	UPROPERTY(EditAnywhere, Category = "Parameters", meta = (TitleProperty = "Name"))
	TArray<FPrimitiveDataLegendEntry> Parameters;

	/**
	 * Materials and material functions laid out by this legend. Their parameters named like an entry
	 * here are switched to custom primitive data and given that entry's slot. Material instances cannot
	 * be bound - the custom primitive data setting lives on the parent's parameter node.
	 *
	 * Right-click a material or function > Bind to Primitive Data Legend is the easy way in; a material
	 * can belong to one legend at a time.
	 */
	UPROPERTY(EditAnywhere, Category = "Binding", meta = (AllowedClasses = "/Script/Engine.Material,/Script/Engine.MaterialFunction"))
	TArray<TSoftObjectPtr<UObject>> BoundMaterials;

	/**
	 * Report a bound material that lacks one of this legend's parameters as an error rather than a
	 * warning. Either way the reverse is always an error: a bound material reading custom primitive
	 * data this legend does not name.
	 */
	UPROPERTY(EditAnywhere, Category = "Binding")
	bool bRequireAllParameters = true;

	const FPrimitiveDataLegendEntry* FindParameter(const FGuid& Id) const;
	const FPrimitiveDataLegendEntry* FindParameter(FName Name) const;

	/** Floats claimed by parameters that have a slot. */
	int32 GetUsedFloats() const;

	/** Custom primitive data floats per primitive - FCustomPrimitiveData::NumCustomPrimitiveDataFloats. */
	static int32 GetCapacity();

	/**
	 * Fired after an edit changes anything a bound material or a node depends on: a name, a type, a
	 * slot, or the binding list itself. The editor module listens, re-syncs materials and refreshes
	 * nodes - this module cannot, as both need editor-only code.
	 */
	static FOnPrimitiveDataLegendChanged OnChanged;

	//~ UObject
	virtual bool IsEditorOnly() const override { return true; }
	virtual void PostInitProperties() override;
	virtual void PostLoad() override;
#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& Event) override;
	virtual EDataValidationResult IsDataValid(class FDataValidationContext& Context) const override;
#endif

private:
	/** Fill in missing ids, names and slots. True if anything changed. */
	bool Normalize();

	/** Hashes of what OnChanged listeners care about, to tell a real change from a no-op edit. */
	uint32 ComputeLayoutHash() const;
	uint32 ComputeBindingHash() const;

	/** Remember the current names and hash as the baseline the next edit is compared against. */
	void TakeSnapshot();

	uint32 LastLayoutHash = 0;
	uint32 LastBindingHash = 0;
	TMap<FGuid, FName> LastNames;
};
