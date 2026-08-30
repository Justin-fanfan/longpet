#pragma once

#include "model/WeatherModels.h"

#include <QString>

// 读取独立的 /etc/longpet/longpet-weather.ini，环境变量优先于 INI、INI 优先于默认值。
// 天气配置不放进 ai.ini，避免与 AI 服务配置耦合。
class WeatherConfigRepository final {
public:
    explicit WeatherConfigRepository(QString path);

    WeatherConfiguration load(QString* error = nullptr) const;
    QString path() const;

private:
    QString m_path;
};
