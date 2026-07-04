#ifndef VORONOI_SPHERE_H
#define VORONOI_SPHERE_H

#include "core/math/vector3.h"
#include "core/templates/vector.h"

// Fibonacci-lattice Voronoi diagram on a unit sphere with cubemap spatial grid
// acceleration. NOT a Godot class — plain C++ used internally by PlanetTectonics
// and EdenPlanetGenerator.
//
// Port of: Projects/planet-voxels/Scripts/planet/voronoi_sphere.gd

class VoronoiSphere {
public:
	VoronoiSphere();

	void generate(int p_num_points, int p_seed);
	int get_num_points() const;

	// Nearest-point queries (thread-safe after generate()).
	int get_nearest(const Vector3 &p_dir) const;
	void get_two_nearest(const Vector3 &p_dir, int &r_nearest, int &r_second,
			float &r_best_dot, float &r_second_dot) const;

	// Direct access to point positions.
	const Vector3 &get_point(int p_index) const;
	const Vector<Vector3> &get_points() const;

private:
	// Cubemap spatial grid for fast nearest-point lookup.
	static const int GRID_RES = 16;

	struct GridCell {
		int start = 0;
		int count = 0;
	};

	void _dir_to_face_uv(const Vector3 &p_dir, int &r_face, float &r_u, float &r_v) const;
	void _build_grid();

	Vector<Vector3> points;
	GridCell grid_cells[6 * GRID_RES * GRID_RES];
	Vector<int> grid_indices;
};

#endif // VORONOI_SPHERE_H
