//
// Created by Dariusz Niedoba on 05.01.2019.
//

#ifndef BZ_MATH_H
#define BZ_MATH_H

#include <cmath>

#define M_PI 3.14159265358979323846

inline int rounded(float x)
{
	if (x < 0.0F)
	{
		x -= 0.5F;
	}
	else
	{
		x += 0.5F;
	}
	return static_cast<int>(x);
}

inline float rad_to_deg(float rad)
{
	return (180.0F / static_cast<float>(M_PI)) * rad;
}

inline int atan2_round_degree(int dx, int dy)
{
	if (dx == 0)
	{
		return 90;
	}
	return rounded(rad_to_deg(atanf(static_cast<float>(dy) / static_cast<float>(dx))));
}

inline bool check(int a, int b)
{
	if (b > 0)
	{
		if (a == b - 180)
		{
			return true;
		}
	}
	else
	{
		if (a == b + 180)
		{
			return true;
		}
	}
	return false;
}

inline int normalize_angle(int deg)
{
	if (deg > 180)
	{
		return deg - 360;
	}
	if (deg <= -180)
	{
		return deg + 360;
	}
	return deg;
}

inline int squared(int x)
{
	return x * x;
}

inline int calculate_slope_in_degrees(int dx, int dy)
{
	if (dx != 0)
	{
		float fi = rad_to_deg(atanf(static_cast<float>(dy) / static_cast<float>(dx)));
		if (fi < 0.0F)
		{
			if (dx < 0)
			{
				fi += 180.0f;
			}
		}
		else
		{
			if (dx < 0)
			{
				fi -= 180.0F;
			}
		}
		int fi2 = rounded(fi);
		if (fi2 <= -180)
		{
			fi2 += 360;
		}
		return fi2;
	}
	return dy <= 0 ? -90 : 90;
}

#endif //BZ_MATH_H
