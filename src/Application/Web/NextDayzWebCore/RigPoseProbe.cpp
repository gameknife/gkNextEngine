#include "Engine/Common/CoreMinimal.hpp"

#include "Application/Game/NextDayz/Core/RigBinary.hpp"
#include "Gameplay/Rig/RigPoseOps.h"

#include <fstream>
#include <iomanip>
#include <iostream>
#include <iterator>

#include <glm/gtc/type_ptr.hpp>

int main(int argc, const char* argv[])
{
    if (argc != 2) return 2;
    try
    {
        std::ifstream file(argv[1], std::ios::binary);
        if (!file) throw std::runtime_error("Cannot open rig file");
        const std::vector<char> bytes{std::istreambuf_iterator<char>(file), {}};
        const auto rig = NextDayz::ReadBakedRig(std::as_bytes(std::span(bytes)));
        NextGameplay::FRigLayeredAnimator animator;
        animator.BindPose(&rig);
        const auto layer = animator.CreateLayer("probe", NextGameplay::ERigLayerBlendMode::Override,
                                                NextGameplay::FRigBoneMask::FullBody(rig));
        std::vector<glm::mat4> matrices;
        std::cout << std::setprecision(9);
        for (const auto& clip : rig.clips)
        {
            for (const auto time : {0.f, .17f, .33f, .5f, .79f, .92f, 1.f})
            {
                animator.SetManualBlend(layer, {{&clip, 1.f}}, time);
                animator.Evaluate(0.f);
                NextGameplay::ComputeRigComponentTransforms(rig, animator.Pose(), matrices);
                for (const auto& matrix : matrices)
                {
                    const auto* data = glm::value_ptr(matrix);
                    for (int i = 0; i < 16; ++i) std::cout << data[i] << '\n';
                }
            }
        }
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
