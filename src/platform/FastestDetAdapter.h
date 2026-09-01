#pragma once

#include "services/VisionPorts.h"

#include <memory>

struct FastestDetConfiguration {
    QString modelPath;
    float confidenceThreshold = 0.65F;
    float nmsThreshold = 0.45F;
    int inferenceThreads = 1;

    static FastestDetConfiguration fromEnvironment();
};

class FastestDetAdapter final : public VisionDetectorPort {
public:
    explicit FastestDetAdapter(
        FastestDetConfiguration configuration =
            FastestDetConfiguration::fromEnvironment());
    ~FastestDetAdapter() override;

    bool initialize(QString* error = nullptr) override;
    bool isAvailable() const override;
    VisionFrameResult detect(const CameraFrame& frame,
                             QString* error = nullptr) override;
    VisionDetectorInfo info() const override;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
    FastestDetConfiguration m_configuration;
    VisionDetectorInfo m_info;
    bool m_available = false;
};
