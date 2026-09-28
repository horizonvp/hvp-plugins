// HVPPalette - written by Claude (Anthropic) in a pairing session with Gideon.

#pragma once

#include "CoreMinimal.h"

class UMaterialParameterCollection;
class UTexture2D;

/**
 * The other direction: right-click a texture, give it a grid, get a Material Parameter Collection
 * whose vector parameters are that texture's swatches.
 *
 * This is the inverse of FHVPPaletteGenerator's swatch texture, and deliberately agrees with it
 * on every convention - row-major indexing from the top left, trailing transparent cells meaning
 * "no entry" - so a palette can round-trip. It is equally happy with a palette PNG somebody handed
 * you, which is the case it was actually built for.
 *
 * UPDATING PRESERVES IDENTITY. A collection parameter is bound by its FGuid, not its name: the Id
 * field exists, in Epic's words, "for fixing up materials that reference this parameter when
 * renaming". So re-reading a texture into an existing collection keeps each index's existing Id AND
 * name and rewrites only the colour. Rebuilding the array wholesale would hand every material a new
 * GUID to resolve and silently turn every reference black.
 */
class FHVPPaletteFromTexture
{
public:
	/** Adds the context-menu entry to Texture2D assets. Call from StartupModule. */
	static void RegisterMenus();

	/**
	 * Reads Texture as a GridX by GridY grid of swatches and writes them into the collection beside
	 * it, creating it if needed. Returns null if the texture could not be read or the grid was
	 * rejected.
	 */
	static UMaterialParameterCollection* BuildFromTexture(UTexture2D* Texture, int32 GridX, int32 GridY);

	/**
	 * Modal grid prompt. Returns false if cancelled; InOutX/InOutY carry the defaults in and the
	 * chosen values out.
	 */
	static bool PromptForGrid(int32& InOutX, int32& InOutY);
};
