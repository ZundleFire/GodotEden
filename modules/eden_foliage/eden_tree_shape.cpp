#include "eden_tree_shape.h"

#define EDEN_SHAPE_BIND_INT(m_name) \
	ClassDB::bind_method(D_METHOD("set_" #m_name, "value"), &EdenTreeShape::set_##m_name); \
	ClassDB::bind_method(D_METHOD("get_" #m_name), &EdenTreeShape::get_##m_name); \
	ClassDB::add_property("EdenTreeShape", PropertyInfo(Variant::INT, #m_name), "set_" #m_name, "get_" #m_name);
#define EDEN_SHAPE_BIND_FLOAT(m_name) \
	ClassDB::bind_method(D_METHOD("set_" #m_name, "value"), &EdenTreeShape::set_##m_name); \
	ClassDB::bind_method(D_METHOD("get_" #m_name), &EdenTreeShape::get_##m_name); \
	ClassDB::add_property("EdenTreeShape", PropertyInfo(Variant::FLOAT, #m_name), "set_" #m_name, "get_" #m_name);

void EdenTreeShape::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_tree_type", "value"), &EdenTreeShape::set_tree_type);
	ClassDB::bind_method(D_METHOD("get_tree_type"), &EdenTreeShape::get_tree_type);
	ClassDB::add_property("EdenTreeShape", PropertyInfo(Variant::INT, "tree_type", PROPERTY_HINT_ENUM, "Oak,Pine,Birch,Willow,Palm,Dead,Fruit"), "set_tree_type", "get_tree_type");

	ClassDB::bind_method(D_METHOD("set_season", "value"), &EdenTreeShape::set_season);
	ClassDB::bind_method(D_METHOD("get_season"), &EdenTreeShape::get_season);
	ClassDB::add_property("EdenTreeShape", PropertyInfo(Variant::INT, "season", PROPERTY_HINT_ENUM, "Spring,Summer,Fall,Winter"), "set_season", "get_season");

	EDEN_SHAPE_BIND_FLOAT(trunk_height)
	EDEN_SHAPE_BIND_FLOAT(trunk_base_radius)
	EDEN_SHAPE_BIND_FLOAT(crown_radius_mul)
	EDEN_SHAPE_BIND_INT(trunk_segments)
	EDEN_SHAPE_BIND_FLOAT(trunk_curve)
	EDEN_SHAPE_BIND_INT(branch_count)
	EDEN_SHAPE_BIND_FLOAT(fork_t_min)
	EDEN_SHAPE_BIND_FLOAT(fork_t_max)
	EDEN_SHAPE_BIND_FLOAT(branch_len_min)
	EDEN_SHAPE_BIND_FLOAT(branch_len_max)
	EDEN_SHAPE_BIND_FLOAT(branch_tilt_min)
	EDEN_SHAPE_BIND_FLOAT(branch_tilt_max)
	EDEN_SHAPE_BIND_FLOAT(branch_len_taper)
	EDEN_SHAPE_BIND_FLOAT(branch_lift_min)
	EDEN_SHAPE_BIND_FLOAT(branch_lift_max)
	EDEN_SHAPE_BIND_INT(crown_twig_count_min)
	EDEN_SHAPE_BIND_INT(crown_twig_count_max)
	EDEN_SHAPE_BIND_FLOAT(crown_twig_len_min)
	EDEN_SHAPE_BIND_FLOAT(crown_twig_len_max)
	EDEN_SHAPE_BIND_INT(sides)
	EDEN_SHAPE_BIND_FLOAT(leaf_radius_mul)
	EDEN_SHAPE_BIND_INT(leaf_cluster_min)
	EDEN_SHAPE_BIND_INT(leaf_cluster_max)
	EDEN_SHAPE_BIND_FLOAT(leaf_jitter)
	ClassDB::bind_method(D_METHOD("set_leaf_subdivisions", "value"), &EdenTreeShape::set_leaf_subdivisions);
	ClassDB::bind_method(D_METHOD("get_leaf_subdivisions"), &EdenTreeShape::get_leaf_subdivisions);
	ClassDB::add_property("EdenTreeShape", PropertyInfo(Variant::INT, "leaf_subdivisions", PROPERTY_HINT_RANGE, "0,4,1"), "set_leaf_subdivisions", "get_leaf_subdivisions");
	EDEN_SHAPE_BIND_FLOAT(leaf_width_mul)
	EDEN_SHAPE_BIND_FLOAT(leaf_height_mul)
	EDEN_SHAPE_BIND_INT(sub_branch_count_min)
	EDEN_SHAPE_BIND_INT(sub_branch_count_max)

	BIND_ENUM_CONSTANT(TREE_OAK);
	BIND_ENUM_CONSTANT(TREE_PINE);
	BIND_ENUM_CONSTANT(TREE_BIRCH);
	BIND_ENUM_CONSTANT(TREE_WILLOW);
	BIND_ENUM_CONSTANT(TREE_PALM);
	BIND_ENUM_CONSTANT(TREE_DEAD);
	BIND_ENUM_CONSTANT(TREE_FRUIT);
	BIND_ENUM_CONSTANT(SEASON_SPRING);
	BIND_ENUM_CONSTANT(SEASON_SUMMER);
	BIND_ENUM_CONSTANT(SEASON_FALL);
	BIND_ENUM_CONSTANT(SEASON_WINTER);
}

#undef EDEN_SHAPE_BIND_INT
#undef EDEN_SHAPE_BIND_FLOAT
