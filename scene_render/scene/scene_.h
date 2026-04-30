#pragma once

namespace wz::scene
{
	struct Scene;

	/*
	splits nodes into:
		static list
		animated list
	*/
	void classify_scene(Scene&);

	/*
	generates linear evaluation list for animated nodes
	*/
	void build_update_order(Scene&);

	/*
	process only dirty roots
	traverse subtree
	compute world transforms

	sparse
	cache-efficient
	event-driven
	*/
	void update_static(Scene&);

	/*
	updates bounding volumes in world space
	used for culling
	*/
	void compute_bounds(Scene&);

	/*
	flattens hot subtrees
	improves traversal locality
	reduces depth cost
		*/
	void optimize_hierarchy(Scene&);

	/*
	recalculates static/dynamic split
	triggered when scene changes significantly
	*/
	void rebuild_static_partition(Scene&);
}