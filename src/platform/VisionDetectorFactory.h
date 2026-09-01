#pragma once

#include <memory>

class VisionDetectorPort;

class VisionDetectorFactory final {
public:
    static std::unique_ptr<VisionDetectorPort> createFromEnvironment();
};
