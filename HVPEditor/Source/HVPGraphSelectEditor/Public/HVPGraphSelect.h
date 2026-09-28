#pragma once

#include "CoreMinimal.h"

class UEdGraphNode;

/** Which traversal "Select Connected" runs. */
enum class EHVPGraphSelectMode : uint8
{
	/** The node, everything its exec output eventually reaches, and the pure inputs of all of them. */
	Downstream,

	/** The node, everything that eventually reaches its exec input, and the pure inputs of all of them. */
	Upstream,

	/**
	 * The linear run the node sits in: walk both ways along exec until a fork or a merge, and take
	 * the pure inputs of everything collected. What "fork" and "merge" mean is a wire count, set in
	 * the plugin's settings.
	 */
	LocalBranch,

	/** Just the node and the pure nodes feeding it, recursively. No exec traversal at all. */
	NodeAndContext,
};

/**
 * "Select Connected" on the Blueprint node context menu.
 *
 * WHY A WIRE COUNT AND NOT A PIN COUNT. A Branch declares two exec outputs whether or not both are
 * used; a Sequence declares as many as you add. Judging a fork by declared pins would end a run at
 * every Branch in the graph, including the ones with a single arm wired, which is not how anyone
 * reads their own graph. Counting connected wires matches the shape you actually see.
 *
 * WHY PURE NODES COME ALONG. Every mode drags in the pure closure feeding each selected node. A
 * selection you cannot move is not much use, and a Get or an Add left behind is a wire stretched
 * across the graph.
 *
 * Nothing here writes to the graph. It reads connectivity and sets the editor's selection, so it
 * cannot dirty an asset and leaves nothing behind if the plugin is removed.
 */
class FHVPGraphSelect
{
public:
	/** Adds the "Select Connected" submenu to every K2 node's context menu. Call from StartupModule. */
	static void RegisterMenus();

	/** Runs the traversal and applies the result as the graph editor's selection. */
	static void SelectFrom(UEdGraphNode* StartNode, EHVPGraphSelectMode Mode);

	/**
	 * The traversal on its own, with no UI. StartNode is always included, even when it is itself a
	 * fork or a merge - a command that could select nothing would be a poor surprise.
	 */
	static TSet<UEdGraphNode*> Gather(UEdGraphNode* StartNode, EHVPGraphSelectMode Mode);
};
