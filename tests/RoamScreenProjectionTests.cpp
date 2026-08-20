#include "algorithms/RoamGeometry.h"
#include "algorithms/RoamScreenError.h"
#include "algorithms/RoamScreenProjection.h"
#include "algorithms/TerrainLodView.h"

#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>

namespace
{
using ParallelRoam::Algorithms::BuildTerrainLodViewInput;
using ParallelRoam::Algorithms::TerrainLodFrustumPlane;
using ParallelRoam::Algorithms::Roam::ArtificialMaximumScreenError;
using ParallelRoam::Algorithms::Roam::ComputeConservativeScreenDistortionPixels;
using ParallelRoam::Algorithms::Roam::ComputeScreenErrorScore;
using ParallelRoam::Algorithms::Roam::ConservativeScreenProjectionInput;
using ParallelRoam::Algorithms::Roam::IsTriangleVisible;
using ParallelRoam::Algorithms::Roam::ScreenErrorScoreInput;
using ParallelRoam::Algorithms::Roam::SplitTriangleDomain;

struct TestTriangleDomain
{
    glm::vec2 A{0.0F};
    glm::vec2 B{0.0F};
    glm::vec2 C{0.0F};
};

float ProjectedSegmentLengthPixels(
    const glm::vec3& point,
    float worldThickness,
    const glm::mat4& viewProjection,
    std::uint32_t drawableWidth,
    std::uint32_t drawableHeight)
{
    const glm::vec3 thickness{0.0F, worldThickness, 0.0F};
    const glm::vec4 positiveClip = viewProjection * glm::vec4{point + thickness, 1.0F};
    const glm::vec4 negativeClip = viewProjection * glm::vec4{point - thickness, 1.0F};
    const glm::vec2 positivePixels{
        static_cast<float>(drawableWidth) * 0.5F * positiveClip.x / positiveClip.w,
        static_cast<float>(drawableHeight) * 0.5F * positiveClip.y / positiveClip.w,
    };
    const glm::vec2 negativePixels{
        static_cast<float>(drawableWidth) * 0.5F * negativeClip.x / negativeClip.w,
        static_cast<float>(drawableHeight) * 0.5F * negativeClip.y / negativeClip.w,
    };
    return glm::length(positivePixels - negativePixels);
}

bool DenseSamplesStayWithinBound(
    const std::array<glm::vec3, 3U>& triangle,
    const glm::mat4& view,
    const glm::mat4& projection,
    float thickness,
    std::uint32_t width,
    std::uint32_t height)
{
    const auto viewInput = BuildTerrainLodViewInput(
        view,
        projection,
        glm::vec3{0.0F},
        glm::vec3{0.0F, 0.0F, -1.0F},
        width,
        height,
        false);
    const std::size_t nearIndex = static_cast<std::size_t>(TerrainLodFrustumPlane::Near);
    const float bound = ComputeConservativeScreenDistortionPixels({
        triangle,
        viewInput.ViewProjection,
        viewInput.FrustumPlanes[nearIndex],
        thickness,
        width,
        height,
    });
    if (!std::isfinite(bound) || bound == ArtificialMaximumScreenError)
    {
        return false;
    }

    constexpr int SubdivisionCount = 48;
    for (int aStep = 0; aStep <= SubdivisionCount; ++aStep)
    {
        for (int bStep = 0; bStep <= SubdivisionCount - aStep; ++bStep)
        {
            const float aWeight = static_cast<float>(aStep) / static_cast<float>(SubdivisionCount);
            const float bWeight = static_cast<float>(bStep) / static_cast<float>(SubdivisionCount);
            const float cWeight = 1.0F - aWeight - bWeight;
            const glm::vec3 point =
                triangle[0] * aWeight + triangle[1] * bWeight + triangle[2] * cWeight;
            const float sampledLength = ProjectedSegmentLengthPixels(
                point,
                thickness,
                viewInput.ViewProjection,
                width,
                height);
            const float tolerance = std::max(1.0e-3F, bound * 2.0e-5F);
            if (!std::isfinite(sampledLength) || sampledLength > bound + tolerance)
            {
                std::cerr << "sampled projected segment exceeded conservative bound: "
                          << sampledLength << " > " << bound << '\n';
                return false;
            }
        }
    }
    return true;
}
} // namespace

int main()
{
    bool passed = true;

    const TestTriangleDomain splitInput{
        glm::vec2{0.0F, 1.0F},
        glm::vec2{1.0F, 0.0F},
        glm::vec2{0.0F, 0.0F},
    };
    const auto splitChildren = SplitTriangleDomain(splitInput);
    const glm::vec2 expectedMidpoint{0.5F, 0.5F};
    passed &= splitChildren.Left.A == splitInput.C;
    passed &= splitChildren.Left.B == splitInput.A;
    passed &= splitChildren.Left.C == expectedMidpoint;
    passed &= splitChildren.Right.A == splitInput.B;
    passed &= splitChildren.Right.B == splitInput.C;
    passed &= splitChildren.Right.C == expectedMidpoint;

    const std::array<glm::vec3, 3U> triangle{
        glm::vec3{-1.4F, -0.2F, 0.8F},
        glm::vec3{1.1F, 0.35F, 0.25F},
        glm::vec3{-0.3F, 0.1F, -1.3F},
    };

    // 多组相机姿态覆盖 thickness vector 在 camera depth 上具有非零分量的情况。
    const std::array<glm::vec3, 3U> cameraPositions{
        glm::vec3{3.0F, 2.8F, 5.0F},
        glm::vec3{-4.0F, 3.5F, 2.5F},
        glm::vec3{1.0F, 5.0F, -4.0F},
    };
    const std::array<glm::vec3, 3U> cameraUps{
        glm::normalize(glm::vec3{0.2F, 1.0F, 0.1F}),
        glm::normalize(glm::vec3{-0.3F, 1.0F, 0.25F}),
        glm::normalize(glm::vec3{0.4F, 0.8F, -0.2F}),
    };
    const std::array<float, 3U> fieldsOfView{42.0F, 60.0F, 91.0F};
    const std::array<std::uint32_t, 3U> widths{960U, 1280U, 1920U};
    const std::array<std::uint32_t, 3U> heights{720U, 720U, 1080U};
    for (std::size_t index = 0U; index < cameraPositions.size(); ++index)
    {
        const glm::mat4 view = glm::lookAtRH(cameraPositions[index], glm::vec3{0.0F}, cameraUps[index]);
        const float aspect = static_cast<float>(widths[index]) / static_cast<float>(heights[index]);
        const glm::mat4 projection = glm::perspectiveRH_NO(
            glm::radians(fieldsOfView[index]),
            aspect,
            0.1F,
            100.0F);
        passed &= DenseSamplesStayWithinBound(
            triangle,
            view,
            projection,
            0.32F,
            widths[index],
            heights[index]);
    }

    // 公式采用齐次 clip x/y/w，因此正交投影也退化为精确的线性 segment 投影。
    const glm::mat4 orthographicView = glm::lookAtRH(
        glm::vec3{2.0F, 3.0F, 5.0F},
        glm::vec3{0.0F},
        glm::vec3{0.0F, 1.0F, 0.0F});
    passed &= DenseSamplesStayWithinBound(
        triangle,
        orthographicView,
        glm::orthoRH_NO(-3.0F, 3.0F, -2.0F, 2.0F, 0.1F, 100.0F),
        0.32F,
        1280U,
        720U);

    // 任一角点的 thickness segment 触碰 near plane 时必须返回人工最大优先级。
    const ConservativeScreenProjectionInput nearCrossing{
        std::array<glm::vec3, 3U>{
            glm::vec3{-1.0F, 0.1F, 0.0F},
            glm::vec3{1.0F, 0.4F, 0.0F},
            glm::vec3{0.0F, 0.5F, 0.0F},
        },
        glm::mat4{1.0F},
        glm::vec4{0.0F, 1.0F, 0.0F, 0.0F},
        0.2F,
        1280U,
        720U,
    };
    passed &= ComputeConservativeScreenDistortionPixels(nearCrossing) == ArtificialMaximumScreenError;

    // 同一投影和相机下同时将两个 drawable 维度翻倍，像素上界也应精确翻倍。
    const glm::mat4 scaleView = glm::lookAtRH(
        glm::vec3{3.0F, 2.8F, 5.0F},
        glm::vec3{0.0F},
        glm::normalize(glm::vec3{0.2F, 1.0F, 0.1F}));
    const glm::mat4 scaleProjection = glm::perspectiveRH_NO(glm::radians(60.0F), 16.0F / 9.0F, 0.1F, 100.0F);
    const auto scaleViewInput = BuildTerrainLodViewInput(
        scaleView,
        scaleProjection,
        glm::vec3{0.0F},
        glm::vec3{0.0F, 0.0F, -1.0F},
        1280U,
        720U,
        false);
    const std::size_t nearIndex = static_cast<std::size_t>(TerrainLodFrustumPlane::Near);
    passed &= IsTriangleVisible(triangle, 0.32F, scaleViewInput.FrustumPlanes);
    const std::array<glm::vec3, 3U> outsideTriangle{
        glm::vec3{1000.0F, 1000.0F, 1000.0F},
        glm::vec3{1001.0F, 1000.0F, 1000.0F},
        glm::vec3{1000.0F, 1001.0F, 1000.0F},
    };
    passed &= !IsTriangleVisible(outsideTriangle, 0.0F, scaleViewInput.FrustumPlanes);

    const float sharedScore = ComputeScreenErrorScore(ScreenErrorScoreInput{
        triangle,
        0.32F,
        scaleViewInput.ViewProjection,
        scaleViewInput.FrustumPlanes[nearIndex],
        scaleViewInput.FrustumPlanes,
        1280U,
        720U,
    });
    passed &= std::isfinite(sharedScore) && sharedScore > 0.0F;
    passed &= ComputeScreenErrorScore(ScreenErrorScoreInput{
        outsideTriangle,
        0.0F,
        scaleViewInput.ViewProjection,
        scaleViewInput.FrustumPlanes[nearIndex],
        scaleViewInput.FrustumPlanes,
        1280U,
        720U,
    }) == 0.0F;

    ConservativeScreenProjectionInput scaleInput{
        triangle,
        scaleViewInput.ViewProjection,
        scaleViewInput.FrustumPlanes[nearIndex],
        0.32F,
        1280U,
        720U,
    };
    const float normalResolutionBound = ComputeConservativeScreenDistortionPixels(scaleInput);
    scaleInput.DrawableWidth *= 2U;
    scaleInput.DrawableHeight *= 2U;
    const float doubleResolutionBound = ComputeConservativeScreenDistortionPixels(scaleInput);
    passed &= std::abs(doubleResolutionBound - normalResolutionBound * 2.0F) <=
        std::max(1.0e-4F, normalResolutionBound * 1.0e-5F);

    // child midpoint 相对 parent plane 位移 0.35；公式 (1) 使 parent thickness=0.20+0.35。
    const glm::vec3 parentA{-2.0F, 0.0F, 1.0F};
    const glm::vec3 parentB{2.0F, 0.0F, 1.0F};
    const glm::vec3 parentC{0.0F, 0.0F, -2.0F};
    const glm::vec3 displacedMidpoint{0.0F, 0.35F, 1.0F};
    ConservativeScreenProjectionInput parentInput{
        std::array<glm::vec3, 3U>{parentA, parentB, parentC},
        scaleViewInput.ViewProjection,
        scaleViewInput.FrustumPlanes[nearIndex],
        0.55F,
        1280U,
        720U,
    };
    ConservativeScreenProjectionInput leftChildInput{
        std::array<glm::vec3, 3U>{parentC, parentA, displacedMidpoint},
        scaleViewInput.ViewProjection,
        scaleViewInput.FrustumPlanes[nearIndex],
        0.20F,
        1280U,
        720U,
    };
    ConservativeScreenProjectionInput rightChildInput{
        std::array<glm::vec3, 3U>{parentB, parentC, displacedMidpoint},
        scaleViewInput.ViewProjection,
        scaleViewInput.FrustumPlanes[nearIndex],
        0.20F,
        1280U,
        720U,
    };
    const float parentBound = ComputeConservativeScreenDistortionPixels(parentInput);
    passed &= ComputeConservativeScreenDistortionPixels(leftChildInput) <= parentBound;
    passed &= ComputeConservativeScreenDistortionPixels(rightChildInput) <= parentBound;

    if (!passed)
    {
        std::cerr << "ROAM conservative screen projection tests failed.\n";
        return 1;
    }

    std::cout << "ROAM conservative screen projection tests passed.\n";
    return 0;
}
