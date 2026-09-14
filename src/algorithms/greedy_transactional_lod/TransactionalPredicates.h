#pragma once

#include "algorithms/greedy_transactional_lod/TransactionalTypes.h"

namespace ParallelRoam::Algorithms::GreedyTransactionalLod
{
/// <summary>
/// 二进制几何的过滤谓词，边界不使用 epsilon 吸附或容差焊接
/// </summary>
class TransactionalPredicates
{
public:
    static int Orientation(const Point& a, const Point& b, const Point& c);
    static bool Shape(const Point& a, const Point& b, const Point& c);
    static bool Contains(const Point& q, const Point& a, const Point& b, const Point& c);
    static std::array<double, 3> Barycentric(const Point& q, const Point& a, const Point& b, const Point& c);
};
}
