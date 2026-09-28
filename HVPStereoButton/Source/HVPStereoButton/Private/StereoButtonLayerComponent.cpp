#include "StereoButtonLayerComponent.h"

UStereoButtonLayerComponent::UStereoButtonLayerComponent()
{
	StereoLayerType = SLT_WorldLocked;
	bLiveTexture = true;      // render-target content: the compositor re-reads it every frame
	bSupportsDepth = true;    // occludable — poke-a-hole underlay or compositor depth test, per platform
	bNoAlphaChannel = false;  // the widget's alpha shapes the button
	Priority = 0;
}

void UStereoButtonLayerComponent::EnsureWorldLocked()
{
	if (StereoLayerType != SLT_WorldLocked)
	{
		StereoLayerType = SLT_WorldLocked;
		MarkStereoLayerDirty();
	}
}
