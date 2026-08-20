#include "KeywordSpottingService.h"

#include "platform/KeywordSpottingAdapter.h"

#include <utility>

namespace {
constexpr int SemanticCooldownMs = 2'500;
constexpr int EmergencyCooldownMs = 8'000;
}

KeywordSpottingService::KeywordSpottingService(KeywordSpottingAdapter* adapter,
                                               Clock clock,
                                               QObject* parent)
    : QObject(parent),
      m_adapter(adapter),
      m_clock(clock ? std::move(clock) : [] { return QDateTime::currentDateTime(); })
{
    if (!m_adapter) {
        m_status.summary = QStringLiteral("关键词 Adapter 未配置");
        return;
    }
    m_status = m_adapter->status();
    connect(m_adapter, &KeywordSpottingAdapter::keywordDetected,
            this, &KeywordSpottingService::handleDetection);
    connect(m_adapter, &KeywordSpottingAdapter::statusChanged,
            this, &KeywordSpottingService::handleRuntimeStatus);
}

KeywordSpottingStatus KeywordSpottingService::status() const
{
    return m_status;
}

KeywordSemantic KeywordSpottingService::semanticFor(
    const KeywordDetection& detection)
{
    const QString signal = detection.signal.trimmed().toUpper();
    const QString keyword = detection.keyword.trimmed();
    if (signal == QStringLiteral("GREETING") || keyword == QStringLiteral("你好"))
        return KeywordSemantic::Greeting;
    if (signal == QStringLiteral("EMERGENCY") || keyword == QStringLiteral("救命"))
        return KeywordSemantic::Emergency;
    if (signal == QStringLiteral("STOP") || keyword == QStringLiteral("停止"))
        return KeywordSemantic::Stop;
    if (signal == QStringLiteral("ACKNOWLEDGE")
        || keyword == QStringLiteral("知道了") || keyword == QStringLiteral("好的")) {
        return KeywordSemantic::Acknowledge;
    }
    if (signal == QStringLiteral("COMPLETE")
        || keyword == QStringLiteral("完成了") || keyword == QStringLiteral("吃过了")) {
        return KeywordSemantic::Complete;
    }
    return KeywordSemantic::Unknown;
}

int KeywordSpottingService::defaultCooldownMs()
{
    return SemanticCooldownMs;
}

void KeywordSpottingService::handleDetection(const KeywordDetection& detection)
{
    emit keywordDetected(detection);
    const QDateTime now = m_clock();
    m_status.lastKeyword = detection.keyword;
    m_status.lastDetectedAt = detection.timestamp.isValid()
        ? detection.timestamp : now;
    emit statusChanged(m_status);

    const KeywordSemantic semantic = semanticFor(detection);
    if (semantic == KeywordSemantic::Unknown)
        return;
    const int key = static_cast<int>(semantic);
    const int cooldown = semantic == KeywordSemantic::Emergency
        ? EmergencyCooldownMs : SemanticCooldownMs;
    const QDateTime previous = m_lastSemanticAt.value(key);
    if (previous.isValid() && previous.msecsTo(now) >= 0
        && previous.msecsTo(now) < cooldown) {
        return;
    }
    m_lastSemanticAt.insert(key, now);
    emit semanticDetected(semantic, detection.keyword);
}

void KeywordSpottingService::handleRuntimeStatus(
    const KeywordSpottingStatus& status)
{
    const QString lastKeyword = m_status.lastKeyword;
    const QDateTime lastDetectedAt = m_status.lastDetectedAt;
    m_status = status;
    m_status.lastKeyword = lastKeyword;
    m_status.lastDetectedAt = lastDetectedAt;
    emit statusChanged(m_status);
}
