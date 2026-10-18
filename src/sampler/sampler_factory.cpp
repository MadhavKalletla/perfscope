#include "perfscope/sampler/sampler_factory.hpp"

#if defined(_WIN32)
#include "perfscope/sampler/windows_sampler.hpp"
#else
#include "perfscope/sampler/linux_sampler.hpp"
#endif

namespace perfscope {

std::unique_ptr<ProcessSampler> SamplerFactory::create_sampler() {
#if defined(_WIN32)
    return std::make_unique<WindowsSampler>();
#else
    return std::make_unique<LinuxSampler>();
#endif
}

} // namespace perfscope
