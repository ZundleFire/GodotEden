#ifndef EDEN_TREE_GENERATOR_H
#define EDEN_TREE_GENERATOR_H

#include "core/math/random_number_generator.h"
#include "core/object/ref_counted.h"
#include "core/variant/dictionary.h"
#include "eden_tree_shape.h"

// Builds a whole tree (trunk + branches + crown twigs + foliage, species- and season-aware) on
// top of EdenTreeMesher's low-level beam/leaf-blob meshing. This is the C++ port of what used
// to be EdenTreeBuilder.gd: the earlier GDScript foliage/winding bugs all traced back to a
// second, hand-rolled implementation drifting out of sync with the C++ mesher.
class EdenTreeGenerator : public RefCounted {
	GDCLASS(EdenTreeGenerator, RefCounted);

public:
	// Picks a random species and returns a fully-populated Shape for it.
	static Ref<EdenTreeShape> build_random_shape(const Ref<RandomNumberGenerator> &p_rng);

	// Returns a Shape tuned for one species (EdenTreeShape::TreeType), with per-tree
	// randomization kept within that species' natural range.
	static Ref<EdenTreeShape> species_shape(int p_tree_type, const Ref<RandomNumberGenerator> &p_rng);

	// Builds one tree from p_shape, consuming p_rng for every randomized choice so the same
	// seed always reproduces the same tree. Returns a Dictionary with "trunk_mesh" (ArrayMesh),
	// "foliage_mesh" (ArrayMesh, 0 surfaces for deciduous species in Winter), "leaf_color"
	// (Color), and "trunk_color" (Color, species bark tone).
	static Dictionary build(const Ref<RandomNumberGenerator> &p_rng, const Ref<EdenTreeShape> &p_shape);

protected:
	static void _bind_methods();
};

#endif // EDEN_TREE_GENERATOR_H
