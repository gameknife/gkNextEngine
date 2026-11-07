#pragma once

#include "Engine/Assets/Acceleration/CPUAccelerationStructure.hpp"

namespace Assets::CPU
{
    struct FCPUAccelerationStructureTestAccess
    {
        static void Publish(FCPUAccelerationStructure& accelerationStructure,
                            FCPUAccelerationStructure::SnapshotPtr snapshot)
        {
            accelerationStructure.blasSet_ = snapshot ? snapshot->blasSet : nullptr;
            accelerationStructure.PublishSnapshot(std::move(snapshot));
        }

        static uint64_t Queue(FCPUAccelerationStructure& accelerationStructure,
                              std::shared_ptr<FCPUTLASBuildInput> input)
        {
            input->epoch = accelerationStructure.buildEpoch_.load(std::memory_order_acquire);
            return accelerationStructure.QueueBuildInput(std::move(input));
        }
    };
}
