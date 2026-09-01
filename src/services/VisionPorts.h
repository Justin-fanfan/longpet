#pragma once

#include "model/CameraModels.h"
#include "model/VisionModels.h"

#include <QString>

class VisionDetectorPort {
public:
    virtual ~VisionDetectorPort() = default;

    virtual bool initialize(QString* error = nullptr) = 0;
    virtual bool isAvailable() const = 0;
    virtual VisionFrameResult detect(const CameraFrame& frame,
                                     QString* error = nullptr) = 0;
    virtual VisionDetectorInfo info() const = 0;
};
