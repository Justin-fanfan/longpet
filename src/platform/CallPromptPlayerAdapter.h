#pragma once

#include "services/VideoCallPorts.h"

#include <QProcess>
#include <QTemporaryFile>

#include <memory>

class CallPromptPlayerAdapter final : public CallPromptPlayerPort {
    Q_OBJECT

public:
    explicit CallPromptPlayerAdapter(QObject* parent = nullptr);
    ~CallPromptPlayerAdapter() override;

    bool play(VideoCallMode mode, QString* error = nullptr) override;
    void stop() override;

private:
    void clearTemporaryFile();

    QProcess m_process;
    std::unique_ptr<QTemporaryFile> m_temporaryFile;
    bool m_stopping = false;
};
