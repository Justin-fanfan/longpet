#include "VisionDetectorFactory.h"

#include "FastestDetAdapter.h"
#include "TinyissimoYoloAdapter.h"

#include <QDebug>

std::unique_ptr<VisionDetectorPort>
VisionDetectorFactory::createFromEnvironment()
{
    const QString detector = qEnvironmentVariable("LONGPET_VISION_DETECTOR")
                                 .trimmed().toLower();
    if (detector == QStringLiteral("fastestdet"))
        return std::make_unique<FastestDetAdapter>();

    if (!detector.isEmpty()
        && detector != QStringLiteral("tinyissimo")
        && detector != QStringLiteral("tinyissimo-yolo")) {
        qWarning().noquote()
            << QStringLiteral(
                   "Unknown LONGPET_VISION_DETECTOR '%1'; using tinyissimo")
                   .arg(detector);
    }
    return std::make_unique<TinyissimoYoloAdapter>();
}
