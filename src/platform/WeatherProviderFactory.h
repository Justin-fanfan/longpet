#pragma once

#include "model/WeatherModels.h"

#include <memory>

class WeatherProviderPort;

class WeatherProviderFactory final {
public:
    static std::unique_ptr<WeatherProviderPort> create(
        const WeatherConfiguration& configuration);
};
