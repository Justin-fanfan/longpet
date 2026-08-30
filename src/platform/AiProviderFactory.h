#pragma once

#include "model/AiModels.h"

#include <memory>

class AsrProviderPort;
class LlmProviderPort;
class TtsProviderPort;

class AiProviderFactory final {
public:
    static std::unique_ptr<AsrProviderPort> createAsr(
        const AsrProviderConfiguration& configuration, int timeoutMs);
    static std::unique_ptr<LlmProviderPort> createLlm(
        const LlmProviderConfiguration& configuration, int timeoutMs);
    static std::unique_ptr<TtsProviderPort> createTts(
        const TtsProviderConfiguration& configuration, int timeoutMs);
};
