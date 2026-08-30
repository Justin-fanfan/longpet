#pragma once

#include "model/AiModels.h"

#include <QString>

class AiConfigRepository final {
public:
    explicit AiConfigRepository(QString path);

    AiConfiguration load(QString* error = nullptr) const;
    QString path() const;

private:
    QString m_path;
};
