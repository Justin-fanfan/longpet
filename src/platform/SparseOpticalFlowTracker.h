#pragma once

#include "services/VisionPorts.h"

#include <memory>

struct SparseOpticalFlowTrackerConfiguration {
    double processingScale = 0.5;
    int maximumPoints = 60;
    int minimumPoints = 6;
    double qualityLevel = 0.01;
    double minimumDistance = 5.0;
    double maximumPointError = 20.0;

    static SparseOpticalFlowTrackerConfiguration fromEnvironment();
};

class SparseOpticalFlowTracker final : public VisionTrackerPort {
public:
    explicit SparseOpticalFlowTracker(
        SparseOpticalFlowTrackerConfiguration configuration =
            SparseOpticalFlowTrackerConfiguration::fromEnvironment());
    ~SparseOpticalFlowTracker() override;

    bool start(const CameraFrame& frame,
               const PersonDetection& target,
               QString* error = nullptr) override;
    VisionTrackerResult update(const CameraFrame& frame,
                               QString* error = nullptr) override;
    void reset() override;
    bool isActive() const override;
    VisionTrackerInfo info() const override;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
    SparseOpticalFlowTrackerConfiguration m_configuration;
    VisionTrackerInfo m_info;
    bool m_active = false;
};
