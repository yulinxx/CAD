#pragma once

#include <cstdint>
#include <chrono>
#include <string>
#include <vector>
#include <algorithm>

#include "RenderCoreApi.h"
#include "RenderTypes.h"

struct RENDER_CORE_API RenderFrame
{
    uint64_t frameId{ 0 };

    std::chrono::steady_clock::time_point timestamp;

    std::vector<RenderBatch> batches;

    ImageBuffer colorBuffer;

    std::string description;

    RenderStatistics statistics;

    RenderOverlay overlay;

    bool valid{ false };

    int batchCount() const
    {
        return static_cast<int>(batches.size());
    }

    int totalVertexCount() const
    {
        int count = 0;
        for (const auto& batch : batches)
            count += batch.vertexCount();
        return count;
    }

    int entityCount() const
    {
        std::vector<std::string> ids;
        for (const auto& batch : batches)
        {
            if (!batch.entityId.empty() && std::find(ids.begin(), ids.end(), batch.entityId) == ids.end())
                ids.push_back(batch.entityId);
        }
        return static_cast<int>(ids.size());
    }

    std::string fullDescription() const
    {
        return "[Frame " + std::to_string(frameId) + "] "
            + description + " | "
            + std::to_string(batchCount()) + " batches | "
            + std::to_string(totalVertexCount()) + " verts | "
            + std::to_string(entityCount()) + " ents | "
            + (valid ? "valid" : "INVALID");
    }
};
