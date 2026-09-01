#pragma once

#include "services/VisionPorts.h"

#include <memory>

struct TinyissimoYoloConfiguration {
    QString modelPath;
    float confidenceThreshold = 0.25F;
    float nmsThreshold = 0.45F;
    int inferenceThreads = 1;

    static TinyissimoYoloConfiguration fromEnvironment();
};

class TinyissimoYoloAdapter final : public VisionDetectorPort {
public:
    explicit TinyissimoYoloAdapter(
        TinyissimoYoloConfiguration configuration =
            TinyissimoYoloConfiguration::fromEnvironment());
    ~TinyissimoYoloAdapter() override;

    bool initialize(QString* error = nullptr) override;
    bool isAvailable() const override;
    VisionFrameResult detect(const CameraFrame& frame,
                             QString* error = nullptr) override;
    VisionDetectorInfo info() const override;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
    TinyissimoYoloConfiguration m_configuration;
    VisionDetectorInfo m_info;
    bool m_available = false;
};
