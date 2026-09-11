#pragma once

#include "model/CameraModels.h"

#ifdef LONGPET_HAS_VISION
#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>

inline void applyCameraRotation(const CameraFrame& frame, cv::Mat* image)
{
    if (!image || image->empty())
        return;
    int rotateCode = -1;
    switch (frame.rotationDegrees) {
    case 90:
        rotateCode = cv::ROTATE_90_CLOCKWISE;
        break;
    case 180:
        rotateCode = cv::ROTATE_180;
        break;
    case 270:
        rotateCode = cv::ROTATE_90_COUNTERCLOCKWISE;
        break;
    default:
        return;
    }
    cv::Mat rotated;
    cv::rotate(*image, rotated, rotateCode);
    *image = rotated;
}
#endif
