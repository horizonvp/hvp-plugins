// StereoButtonFace_M (formerly LenovoButtonFace_M) — Custom node body (copy of the node's Code, kept here for diffing).
// Inputs, in this order: UV, Time, ViewOffset(float2), FacePx(float2), CornerPx, MarginPx,
//   ColorTop(float3), ColorBottom(float3), SheenColor(float3), SheenPeriod, SheenSweepSeconds,
//   SheenWidth, Parallax, GlossStrength, GlossHeight, ShadowStrength, ShadowHeight, EdgeDarken,
//   Opacity, PressAmount, SheenTilt, SheenStrength, GradientTop, GradientBottom
// Output: float4 (rgb = colour, unpremultiplied; a = coverage). Material is MD_UI + AlphaComposite.
// Custom-node code is pasted into a function body: no function definitions, and `line` is reserved.

// silhouette: rounded box SDF in pixels (< 0 inside)
float2 p = (UV - 0.5) * FacePx;
float2 b = FacePx * 0.5 - MarginPx - CornerPx;
float2 q = abs(p) - b;
float d = length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - CornerPx;
float fill = 1.0 - smoothstep(-1.0, 1.0, d);

// base: vertical gradient, shifted a little by the view angle (shallow layer)
// GradientTop / GradientBottom: where (0 = top edge, 1 = bottom edge) the gradient starts and ends;
// above GradientTop the face is pure ColorTop, below GradientBottom pure ColorBottom.
float vg = UV.y - ViewOffset.y * Parallax * 0.35;
float vb = smoothstep(min(GradientTop, GradientBottom - 0.001), GradientBottom, vg);
float3 col = lerp(ColorTop, ColorBottom, vb);

// sheen: a tilted band sweeping left to right, once per period, sitting deeper in the glass
float2 uvS = UV + ViewOffset * Parallax;
float u = uvS.x + (uvS.y - 0.5) * SheenTilt;
float period = max(SheenPeriod, 0.01);
float phase = frac(Time / period);
float sweepFrac = saturate(SheenSweepSeconds / period);
float k = saturate(phase / max(sweepFrac, 0.001));
float ease = k * k * (3.0 - 2.0 * k);
float center = lerp(-SheenWidth * 2.5, 1.0 + SheenWidth * 2.5, ease);
float band = exp(-pow((u - center) / max(SheenWidth, 0.001), 2.0));
col += SheenColor * SheenStrength * band;

// press feedback: brighten
col = lerp(col, col * 1.25 + SheenColor * 0.15, saturate(PressAmount));

// vertical glass shading: gloss cap and glint along the top, shadow along the bottom
float v = UV.y - ViewOffset.y * Parallax * 0.25;
float cap = smoothstep(GlossHeight, 0.0, v);
float glint = exp(-pow((v - 0.055) / 0.028, 2.0));
float gloss = GlossStrength * (cap * cap * 0.35 + glint * 0.55);
gloss *= 1.0 - smoothstep(-CornerPx * 0.6, 0.0, d);
col += gloss;
float shadow = ShadowStrength * smoothstep(1.0 - ShadowHeight, 1.0, v);
col *= 1.0 - shadow;
col *= 1.0 - EdgeDarken * smoothstep(-8.0, 0.0, d);

return float4(col, fill * Opacity);
