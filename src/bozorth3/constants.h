//
// Created by Dariusz Niedoba on 05.01.2019.
//

#ifndef BZ_BOZOTH_X_H
#define BZ_BOZOTH_X_H

#include <cassert>
#include <optional>
#include <vector>
#include <span>
#include <cstdint>
#include "types.h"

#define MAX_MINUTIA_DISTANCE            125
#define MAX_MINUTIA_DISTANCE_SQUARED    5625
#define MIN_NUMBER_OF_EDGES             500
#define FACTOR                          0.05F
#define ANGLE_LOWER_BOUND               11
#define ANGLE_UPPER_BOUND               349
#define MIN_NUMBER_OF_PAIRS_TO_CLUSTER  3
#define SCORE_THRESHOLD                 8
#define MAX_NUMBER_OF_GROUPS            10
#define MAX_BOZORTH_MINUTIAE            200
#define MIN_BOZORTH_MINUTIAE            0
#define MAX_NUMBER_OF_PAIRS             20000
#define MAX_NUMBER_OF_EDGES             20000
#define MAX_NUMBER_OF_CLUSTERS          2000
#define MAX_NUMBER_OF_ENDPOINTS         20000


enum class MinutiaKind
{
	Bif = 1,
	Rig = 2
};

struct Minutia
{
	i32 x{};
	i32 y{};
	i32 t{};
	std::optional<MinutiaKind> kind{};
};

struct Pair
{
	i32 delta_theta;
	u32 probe_k;
	u32 probe_j;
	u32 gallery_k;
	u32 gallery_j;
	u32 points;
};

enum OrderKJ
{
	KJ,
	JK
};

struct Edge
{
	i32 distance_squared;
	i32 min_beta;
	i32 max_beta;
	u32 endpoint_k;
	u32 endpoint_j;
	i32 theta_kj;
	enum OrderKJ beta_order;
};

#endif
