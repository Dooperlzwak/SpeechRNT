#pragma once

#include "contextual_transcriber_interface.hpp"
#include <memory>

namespace stt {
namespace advanced {

/**
 * Factory function to create a contextual transcriber instance
 * @return Unique pointer to contextual transcriber interface
 */
std::unique_ptr<ContextualTranscriberInterface> createContextualTranscriber();

} // namespace advanced
} // namespace stt