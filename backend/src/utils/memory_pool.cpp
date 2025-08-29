#include "utils/memory_pool.hpp"
#include "utils/logging.hpp"

namespace utils {

// Template instantiations for commonly used types
template class MemoryPool<AudioBufferPool::AudioBuffer>;
template class MemoryPool<TranscriptionResultPool::TranscriptionResult>;

} // namespace utils