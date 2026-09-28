#pragma once

#include "CoreMinimal.h"

class SGraphPanel;
class UEdGraphNode;

/** Which way the drag locked. The cut line runs perpendicular to this. */
enum class EHVPCardinal : uint8
{
	Up,
	Down,
	Left,
	Right,
};

/**
 * The organisation tool: cut the graph along a line and take everything past it.
 *
 * WHY GRAPH SPACE AND NOT SCREEN SPACE. The rule is "every node in the entire graph", which
 * includes nodes scrolled far off screen. A screen-space test would silently only ever consider
 * what happened to be visible, and would quietly change its answer when the view was panned.
 * Node positions and sizes are read in graph coordinates, so the cut means the same thing
 * regardless of where the view is looking or how far it is zoomed.
 */
class FHVPDirectionalSelect
{
public:
	/** Locks a drag delta to a cardinal. Screen Y grows downward, so negative Y is Up. */
	static EHVPCardinal CardinalForDelta(const FVector2D& Delta);

	/** The axis a cardinal moves along: (1,0), (-1,0), (0,1) or (0,-1). */
	static FVector2D AxisOf(EHVPCardinal Cardinal);

	/**
	 * Every node lying ENTIRELY beyond GraphLinePoint in the given direction.
	 *
	 * Entirely, not merely mostly: a node straddling the line belongs to both halves, and taking it
	 * would drag a node the user can plainly see is on the other side. Nodes whose widget has no
	 * desired size yet are treated as points at their own position rather than skipped, so a node
	 * the panel has not laid out is still cut correctly.
	 */
	static TArray<UEdGraphNode*> NodesBeyond(
		const SGraphPanel& Panel, const FVector2D& GraphLinePoint, EHVPCardinal Cardinal);
};
