#pragma once

#include "CoreMinimal.h"

/**
 * Make HVPText Font...: an entry in a Font Face's Content Browser context menu. A dialog asks for the
 * font id, the folder and the techniques (MSDF quads, glyph meshes); then the font face's data goes
 * through Tools/HVPText/make_font.py, which builds and imports the HVPText font asset. Project
 * Settings > HVP Text gets the result wherever it has nothing set yet.
 */
namespace HVPTextFontMaker
{
	void Register();
	void Unregister();
}
