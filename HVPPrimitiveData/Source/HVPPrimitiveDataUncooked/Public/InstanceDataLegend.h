#pragma once

#include "CoreMinimal.h"
#include "PrimitiveDataLegend.h"
#include "UObject/Object.h"
#include "UObject/SoftObjectPtr.h"

#include "InstanceDataLegend.generated.h"

class UInstanceDataLegend;

UENUM()
enum class EInstanceDataParameterType : uint8
{
	/** One float. Read in a material with a PerInstanceCustomData node. */
	Scalar	UMETA(DisplayName = "Scalar"),
	/** Three consecutive floats. Read with a PerInstanceCustomData3Vector node; set from Blueprint as a Linear Color, alpha ignored. */
	Vector	UMETA(DisplayName = "Vector (RGB)"),
};

/** One named parameter and the per instance custom data slot it owns. */
USTRUCT()
struct HVPPRIMITIVEDATAUNCOOKED_API FInstanceDataLegendEntry
{
	GENERATED_BODY()

	/**
	 * The label, and the link to materials: a bound material's PerInstanceCustomData node whose
	 * Description is exactly this name is the one that gets this slot.
	 */
	UPROPERTY(EditAnywhere, Category = "Parameter")
	FName Name;

	UPROPERTY(EditAnywhere, Category = "Parameter")
	EInstanceDataParameterType Type = EInstanceDataParameterType::Scalar;

	/**
	 * First per instance custom data float this parameter occupies; a vector takes this one and the next
	 * two. Assigned for you, and STABLE - adding, removing or reordering other entries never moves it.
	 * Only this entry changing type to something that no longer fits where it is moves it.
	 */
	UPROPERTY(VisibleAnywhere, Category = "Parameter")
	int32 Slot = INDEX_NONE;

	/** Identity that survives a rename. Blueprint nodes hold this rather than the name. */
	UPROPERTY()
	FGuid Id;

	/** The width Slot was allocated for. Lets the allocator tell "this entry changed" from "a neighbour changed". */
	UPROPERTY()
	int32 AllocatedWidth = 0;

	int32 GetWidth() const { return Type == EInstanceDataParameterType::Vector ? 3 : 1; }
	bool HasSlot() const { return Slot != INDEX_NONE; }
};

// The same shape as the Primitive Data Legend's: legend, renames, whether the layout (not just the binding list) changed.
DECLARE_MULTICAST_DELEGATE_ThreeParams(FOnInstanceDataLegendChanged, UInstanceDataLegend*, const FPrimitiveDataRenames&, bool);

/**
 * A named layout for an instanced static mesh's per instance custom data: which parameter lives in
 * which float of every instance.
 *
 * The Primitive Data Legend's twin, for the other place a material can read data from Blueprint without
 * a dynamic material instance. Parameters are labelled here and slots allocated for them; bound
 * materials have those slots written into their PerInstanceCustomData nodes (matched by the node's
 * Description, as those nodes have no parameter name); Set Named Instance Data picks parameters by name
 * and bakes the slot in when the Blueprint compiles.
 *
 * Unlike custom primitive data there is no fixed number of floats: each instanced mesh has as many as its
 * Num Custom Data Floats says. Floats Per Instance below is the number this legend needs.
 *
 * EDITOR-ONLY, like the Primitive Data Legend: nothing reads it at runtime, so the cooker leaves it out.
 */
UCLASS(meta = (DisplayName = "Instance Data Legend"))
class HVPPRIMITIVEDATAUNCOOKED_API UInstanceDataLegend : public UObject
{
	GENERATED_BODY()

public:
	/** The parameters. A scalar costs one float per instance, a vector three. */
	UPROPERTY(EditAnywhere, Category = "Parameters", meta = (TitleProperty = "Name"))
	TArray<FInstanceDataLegendEntry> Parameters;

	/**
	 * Floats every instance needs to hold this layout: one past the highest slot in use. Set Num Custom
	 * Data Floats on the instanced meshes to at least this; writes past their end are dropped.
	 */
	UPROPERTY(VisibleAnywhere, Transient, Category = "Parameters")
	int32 FloatsPerInstance = 0;

	/**
	 * Materials and material functions laid out by this legend. Their PerInstanceCustomData and
	 * PerInstanceCustomData3Vector nodes whose Description names an entry here are given that entry's
	 * slot. Material instances cannot be bound - the nodes live in the parent.
	 *
	 * Right-click a material or function > Bind to Instance Data Legend is the easy way in; a material
	 * can belong to one instance data legend at a time (and, separately, to one primitive data legend).
	 */
	UPROPERTY(EditAnywhere, Category = "Binding", meta = (AllowedClasses = "/Script/Engine.Material,/Script/Engine.MaterialFunction"))
	TArray<TSoftObjectPtr<UObject>> BoundMaterials;

	/**
	 * Report a bound material that lacks one of this legend's parameters as an error rather than a
	 * warning. Either way the reverse is always an error: a bound material reading per instance custom
	 * data through a node whose Description names nothing in this legend.
	 */
	UPROPERTY(EditAnywhere, Category = "Binding")
	bool bRequireAllParameters = true;

	const FInstanceDataLegendEntry* FindParameter(const FGuid& Id) const;
	const FInstanceDataLegendEntry* FindParameter(FName Name) const;

	/** Floats claimed by parameters that have a slot. */
	int32 GetUsedFloats() const;

	/**
	 * The most floats per instance a legend lays out. The engine sets no limit, but every float is paid
	 * for by every instance, and the multiple node's write mask is 64 bits.
	 */
	static int32 GetCapacity() { return 64; }

	/** Fired after an edit changes a name, a type, a slot, or the binding list. The editor module listens. */
	static FOnInstanceDataLegendChanged OnChanged;

	//~ UObject
	virtual bool IsEditorOnly() const override { return true; }
	virtual void PostInitProperties() override;
	virtual void PostLoad() override;
#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& Event) override;
	virtual EDataValidationResult IsDataValid(class FDataValidationContext& Context) const override;
#endif

private:
	/** Fill in missing ids, names and slots, and recount FloatsPerInstance. True if anything changed. */
	bool Normalize();

	uint32 ComputeLayoutHash() const;
	uint32 ComputeBindingHash() const;
	void TakeSnapshot();

	uint32 LastLayoutHash = 0;
	uint32 LastBindingHash = 0;
	TMap<FGuid, FName> LastNames;
};
