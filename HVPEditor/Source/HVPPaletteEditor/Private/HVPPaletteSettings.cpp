#include "HVPPaletteSettings.h"

// The two pin-based outputs on, the index-based one off. See the property comments in the header:
// the pins fail loudly on a rename, the texture's indices move silently on a reorder, so the safe
// pair is what a project gets without asking for it.
UHVPPaletteSettings::UHVPPaletteSettings()
	: bGenerateMaterialFunction(true)
	, bGenerateBlueprintLibrary(true)
	, bGeneratePaletteTexture(false)
{
}
