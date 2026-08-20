#include "algorithms/RoamNestedWedgie.h"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <vector>

namespace
{
struct TestDomain
{
    std::size_t HeapIndex{0U};
};

struct TestChildren
{
    TestDomain Left;
    TestDomain Right;
};

struct TestPoint
{
    float X{0.0F};
    float Y{0.0F};
};

struct GeometryDomain
{
    TestPoint A;
    TestPoint B;
    TestPoint C;
};

struct GeometryChildren
{
    GeometryDomain Left;
    GeometryDomain Right;
};

bool NearlyEqual(float left, float right)
{
    return std::abs(left - right) <= 0.000001F;
}

bool Check(bool condition, const char* message)
{
    if (!condition)
    {
        std::cerr << message << '\n';
        return false;
    }
    return true;
}

GeometryChildren SplitGeometryDomain(const GeometryDomain& domain)
{
    const TestPoint midpoint{
        (domain.A.X + domain.B.X) * 0.5F,
        (domain.A.Y + domain.B.Y) * 0.5F,
    };
    return GeometryChildren{
        GeometryDomain{domain.C, domain.A, midpoint},
        GeometryDomain{domain.B, domain.C, midpoint},
    };
}

float GeometryHeight(const TestPoint& point)
{
    return point.X * point.X + 0.5F * point.Y * point.Y + 0.3F * point.X * point.Y;
}

float InterpolateTriangleHeight(const GeometryDomain& domain, const TestPoint& point)
{
    const float denominator =
        (domain.B.Y - domain.C.Y) * (domain.A.X - domain.C.X) +
        (domain.C.X - domain.B.X) * (domain.A.Y - domain.C.Y);
    const float weightA =
        ((domain.B.Y - domain.C.Y) * (point.X - domain.C.X) +
         (domain.C.X - domain.B.X) * (point.Y - domain.C.Y)) /
        denominator;
    const float weightB =
        ((domain.C.Y - domain.A.Y) * (point.X - domain.C.X) +
         (domain.A.X - domain.C.X) * (point.Y - domain.C.Y)) /
        denominator;
    const float weightC = 1.0F - weightA - weightB;
    return weightA * GeometryHeight(domain.A) +
           weightB * GeometryHeight(domain.B) +
           weightC * GeometryHeight(domain.C);
}

void CollectFinestVertices(
    const GeometryDomain& domain,
    int depth,
    int finestDepth,
    std::vector<TestPoint>& vertices)
{
    if (depth >= finestDepth)
    {
        vertices.push_back(domain.A);
        vertices.push_back(domain.B);
        vertices.push_back(domain.C);
        return;
    }

    const GeometryChildren children = SplitGeometryDomain(domain);
    CollectFinestVertices(children.Left, depth + 1, finestDepth, vertices);
    CollectFinestVertices(children.Right, depth + 1, finestDepth, vertices);
}

bool CheckNestedBounds(
    const GeometryDomain& domain,
    int depth,
    int finestDepth,
    std::size_t treeIndex,
    const std::vector<float>& tree)
{
    std::vector<TestPoint> descendantVertices;
    CollectFinestVertices(domain, depth, finestDepth, descendantVertices);
    for (const TestPoint& vertex : descendantVertices)
    {
        const float actualError = std::abs(
            GeometryHeight(vertex) - InterpolateTriangleHeight(domain, vertex));
        if (actualError > tree[treeIndex] + 0.000001F)
        {
            return false;
        }
    }

    if (depth >= finestDepth)
    {
        return true;
    }

    const GeometryChildren children = SplitGeometryDomain(domain);
    return CheckNestedBounds(
               children.Left,
               depth + 1,
               finestDepth,
               treeIndex * 2U + 1U,
               tree) &&
           CheckNestedBounds(
               children.Right,
               depth + 1,
               finestDepth,
               treeIndex * 2U + 2U,
               tree);
}
} // namespace

int main()
{
    using namespace ParallelRoam::Algorithms::Roam;

    bool passed = true;
    passed &= Check(ResolveNestedWedgieTreeDepth(129, 129, 0, 20) == 14, "129 height map depth must be 14");
    passed &= Check(ResolveNestedWedgieTreeDepth(513, 513, 14, 20) == 18, "513 height map depth must be 18");
    passed &= Check(ResolveNestedWedgieTreeDepth(129, 129, 20, 20) == 20, "runtime depth must extend the tree");
    passed &= Check(ResolveNestedWedgieTreeDepth(4097, 4097, 14, 20) == 20, "tree depth must respect the cap");
    passed &= Check(CompleteBinaryTreeNodeCount(2) == 7U, "depth-two tree must contain seven nodes");

    // Internal nodes use signed midpoint displacements; finest leaves deliberately
    // carry large values to prove that formula (1) initializes them to zero.
    const std::vector<float> displacements{0.10F, -0.20F, 0.40F, 9.0F, 9.0F, 9.0F, 9.0F};
    std::vector<float> tree;
    const auto split = [](const TestDomain& domain) {
        return TestChildren{
            TestDomain{domain.HeapIndex * 2U + 1U},
            TestDomain{domain.HeapIndex * 2U + 2U},
        };
    };
    const auto displacement = [&displacements](const TestDomain& domain) {
        return displacements[domain.HeapIndex];
    };

    const float rootThickness = BuildNestedWedgieTree(TestDomain{}, 2, tree, split, displacement);
    passed &= Check(tree.size() == 7U, "nested wedgie tree capacity mismatch");
    passed &= Check(NearlyEqual(tree[1], 0.20F), "left parent must use abs(local displacement)");
    passed &= Check(NearlyEqual(tree[2], 0.40F), "right parent thickness mismatch");
    passed &= Check(NearlyEqual(rootThickness, 0.50F), "root must equal max(child thickness) plus local displacement");
    for (std::size_t leaf = 3U; leaf < tree.size(); ++leaf)
    {
        passed &= Check(NearlyEqual(tree[leaf], 0.0F), "finest-level thickness must be zero");
    }

    constexpr int GeometryFinestDepth = 4;
    const GeometryDomain geometryRoot{
        TestPoint{0.0F, 1.0F},
        TestPoint{1.0F, 0.0F},
        TestPoint{0.0F, 0.0F},
    };
    std::vector<float> geometryTree;
    const auto splitGeometry = [](const GeometryDomain& domain) {
        return SplitGeometryDomain(domain);
    };
    const auto signedGeometryDisplacement = [](const GeometryDomain& domain) {
        const TestPoint midpoint{
            (domain.A.X + domain.B.X) * 0.5F,
            (domain.A.Y + domain.B.Y) * 0.5F,
        };
        return GeometryHeight(midpoint) -
               (GeometryHeight(domain.A) + GeometryHeight(domain.B)) * 0.5F;
    };
    static_cast<void>(BuildNestedWedgieTree(
        geometryRoot,
        GeometryFinestDepth,
        geometryTree,
        splitGeometry,
        signedGeometryDisplacement));
    passed &= Check(
        CheckNestedBounds(geometryRoot, 0, GeometryFinestDepth, 0U, geometryTree),
        "every ancestor thickness must bound all finest descendant vertices");

    return passed ? EXIT_SUCCESS : EXIT_FAILURE;
}
