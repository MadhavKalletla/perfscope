#pragma once

#include "perfscope/sampler/process_sampler.hpp"
#include <memory>

namespace perfscope {

class SamplerFactory {
public:
    /**
     * @brief Creates and returns the appropriate ProcessSampler implementation for the current OS.
     */
    static std::unique_ptr<ProcessSampler> create_sampler();
};

} // namespace perfscope
