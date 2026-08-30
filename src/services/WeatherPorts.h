#pragma once

#include "model/WeatherModels.h"

#include <QObject>

// WeatherService 只依赖这个轻量接口，不依赖具体的 QWeatherProvider。
// 以后增加 AmapWeather / OpenWeather / LocalWeather 时无需改动 WeatherService、
// UI 或 VoiceInteractionService，只要在 Composition Root 里换 Provider。
class WeatherProviderPort : public QObject {
    Q_OBJECT

public:
    using QObject::QObject;
    ~WeatherProviderPort() override = default;

    virtual void fetchCurrent() = 0;
    virtual void cancel() = 0;

signals:
    void currentWeatherReady(const WeatherSnapshot& snapshot);
    void fetchFailed(const WeatherError& error);
};
