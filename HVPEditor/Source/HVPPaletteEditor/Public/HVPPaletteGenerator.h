// HVPPalette - written by Claude (Anthropic) in a pairing session with Gideon, who found the
// __WorldContext route that makes the generated function resolve at construction-script time, and
// caught the ParameterId, stale-node and NewFunction bugs that the first drafts shipped with.

#pragma once

#include "CoreMinimal.h"

class UMaterialParameterCollection;
class UMaterialFunction;
class UBlueprint;
class UTexture2D;

/**
 * Right-click a Material Parameter Collection -> "Generate Palette Accessors", and get generated
 * assets that expose every parameter in it as a named pin.
 *
 * WHY: an MPC is already the one thing both materials and Blueprints can read from a single source
 * (materials via a CollectionParameter node, Blueprints via
 * UKismetMaterialLibrary::GetVectorParameterValue). What it is not is SAFE to reference - both
 * sides address parameters by FName string, so a typo or a rename silently yields black instead of
 * failing. These generated wrappers turn that string lookup into a pin you pick from a list.
 *
 * Regeneration updates the existing assets IN PLACE rather than replacing them, so every material
 * and graph already wired to the generated function keeps working. Re-run it whenever the palette
 * gains or loses an entry.
 *
 * THE GENERATED ASSETS DO NOT DEPEND ON THIS PLUGIN. Both are built from Engine nodes only, so a
 * project without HVPPalette installed can still use them - only the collection has to travel
 * alongside. The plugin is a generator, not a runtime dependency, and nothing it produces stops
 * working when it is absent.
 */
class FHVPPaletteGenerator
{
public:
	/** Adds the context-menu entry to Material Parameter Collection assets. Call from StartupModule. */
	static void RegisterMenus();

	/**
	 * Builds (or rebuilds) the material function for one collection: one CollectionParameter node
	 * per parameter, each wired to a named function output. Returns null if nothing could be made.
	 */
	static UMaterialFunction* GenerateMaterialFunction(UMaterialParameterCollection* Collection);

	/**
	 * Builds (or rebuilds) a Blueprint Function Library holding one pure function whose outputs are
	 * the collection's parameters, each driven by Engine's UKismetMaterialLibrary. No World Context
	 * pin on the generated node: the graph feeds each call from __WorldContext, the hidden member
	 * every Blueprint Function Library function carries, which resolves even in a construction script.
	 */
	static UBlueprint* GenerateBlueprintLibrary(UMaterialParameterCollection* Collection);

	/**
	 * Bakes the collection into an 8x8 grid of colour swatches - a literal palette - so a material
	 * can reach any entry with a single 0-63 scalar instead of a pin per colour. Cell 0 is top
	 * left, index = Row * 8 + Column; cells with no parameter behind them are transparent black.
	 *
	 * Index order is the order of the output pins above: vectors in declaration order first, then
	 * scalars (stored as grey with alpha 1). Pin N and cell N are the same palette entry.
	 *
	 * Uncompressed RGBA16F with sRGB off, nearest filtering, no mips: a sample anywhere inside a
	 * cell returns that parameter's FLinearColor unchanged, HDR values included, and neighbouring
	 * swatches never bleed into each other.
	 *
	 * BEWARE what the pins protect you from and this does not. An index is a bare number, so
	 * inserting a colour in the MIDDLE of the collection shifts every index after it and silently
	 * moves what each material samples. Append new entries at the end, and prefer the generated
	 * pins wherever the colour is fixed - the texture is for the cases where the index is computed.
	 * The index-to-name map is written to the log on every generation.
	 */
	static UTexture2D* GeneratePaletteTexture(UMaterialParameterCollection* Collection);
};
