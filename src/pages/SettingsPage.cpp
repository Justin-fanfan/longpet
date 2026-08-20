#include "SettingsPage.h"

#include "widgets/VisualComponents.h"

#include <QGridLayout>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QPushButton>
#include <QSlider>
#include <QStringList>
#include <QTimer>
#include <QVBoxLayout>

namespace {
struct SliderControl {
    QWidget* widget = nullptr;
    QSlider* slider = nullptr;
    QLabel* value = nullptr;
};

SliderControl sliderControl(QWidget* parent)
{
    SliderControl result;
    result.widget = new QWidget(parent);
    result.widget->setFixedWidth(160);
    auto* layout = new QVBoxLayout(result.widget);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    result.value = makeLabel({}, "assist", result.widget);
    result.value->setAlignment(Qt::AlignRight);
    result.slider = new QSlider(Qt::Horizontal, result.widget);
    result.slider->setRange(0, 100);
    result.slider->setFixedHeight(64);
    layout->addWidget(result.value);
    layout->addWidget(result.slider);
    return result;
}

QWidget* summaryControl(QLabel** label, QWidget* parent)
{
    *label = makeLabel({}, "assist", parent);
    (*label)->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    (*label)->setFixedWidth(160);
    (*label)->setWordWrap(true);
    return *label;
}
}

SettingsPage::SettingsPage(QWidget* parent)
    : QWidget(parent)
{
    setProperty("page", true);
    setAttribute(Qt::WA_StyledBackground, true);
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);
    auto* header = new PageHeaderWidget(QStringLiteral("设置"), this);
    root->addWidget(header);
    connect(header, &PageHeaderWidget::backRequested,
            this, &SettingsPage::backRequested);

    auto* grid = new QGridLayout;
    grid->setContentsMargins(32, 16, 32, 32);
    grid->setHorizontalSpacing(32);
    grid->setVerticalSpacing(16);

    const SliderControl volume = sliderControl(this);
    m_volumeSlider = volume.slider;
    m_volumeSlider->setObjectName(QStringLiteral("volumeSlider"));
    m_volumeValue = volume.value;
    const SliderControl brightness = sliderControl(this);
    m_brightnessSlider = brightness.slider;
    m_brightnessSlider->setObjectName(QStringLiteral("brightnessSlider"));
    m_brightnessValue = brightness.value;

    m_soundRow = new SettingRow(QStringLiteral(":/icons/volume.svg"),
        QStringLiteral("声音大小"), QStringLiteral("设备音量待接入"),
        volume.widget, this);
    m_brightnessRow = new SettingRow(QStringLiteral(":/icons/brightness.svg"),
        QStringLiteral("屏幕亮度"), QStringLiteral("设备背光待接入"),
        brightness.widget, this);
    auto* network = new SettingRow(QStringLiteral(":/icons/network.svg"),
        QStringLiteral("网络连接"), QStringLiteral("来自 SystemService"),
        summaryControl(&m_networkSummary, this), this);

    auto* familyButton = new QPushButton(QStringLiteral("开始配对"), this);
    familyButton->setObjectName(QStringLiteral("pairFamilyButton"));
    familyButton->setProperty("role", "secondaryCompact");
    familyButton->setFixedSize(128, 64);
    m_familyRow = new SettingRow(QStringLiteral(":/icons/family.svg"),
        QStringLiteral("家属配对"), QStringLiteral("尚未配对"),
        familyButton, this);

    m_petStyleButton = new QPushButton(this);
    m_petStyleButton->setObjectName(QStringLiteral("petStyleButton"));
    m_petStyleButton->setProperty("role", "secondaryCompact");
    m_petStyleButton->setFixedSize(180, 64);
    auto* pet = new SettingRow(QStringLiteral(":/icons/pet.svg"),
        QStringLiteral("宠物风格"), QStringLiteral("本地用户设置"),
        m_petStyleButton, this);

    auto* about = new SettingRow(QStringLiteral(":/icons/info.svg"),
        QStringLiteral("关于设备"), QStringLiteral("正式软件版本"),
        summaryControl(&m_versionSummary, this), this);
    auto* keywordSpotting = new SettingRow(QStringLiteral(":/icons/activity.svg"),
        QStringLiteral("本地感知"), QStringLiteral("离线语音与视觉"),
        summaryControl(&m_keywordSpottingSummary, this), this);
    m_keywordSpottingSummary->setObjectName(QStringLiteral("keywordSpottingSummary"));
    auto* power = new SettingRow(QStringLiteral(":/icons/battery.svg"),
        QStringLiteral("设备电源"), QStringLiteral("来自设备 Adapter"),
        summaryControl(&m_powerSummary, this), this);
    m_powerSummary->setObjectName(QStringLiteral("powerSummary"));

    grid->addWidget(m_soundRow, 0, 0);
    grid->addWidget(m_brightnessRow, 0, 1);
    grid->addWidget(network, 1, 0);
    grid->addWidget(m_familyRow, 1, 1);
    grid->addWidget(pet, 2, 0);
    grid->addWidget(about, 2, 1);
    grid->addWidget(keywordSpotting, 3, 0);
    grid->addWidget(power, 3, 1);
    grid->setRowStretch(4, 1);
    root->addLayout(grid, 1);

    auto connectSlider = [this](QSlider* slider, QLabel* label, bool isVolume) {
        auto* debounce = new QTimer(slider);
        debounce->setSingleShot(true);
        debounce->setInterval(180);
        connect(slider, &QSlider::valueChanged, this,
                [this, label, debounce, isVolume](int value) {
                    if (isVolume)
                        updateValueLabel(label, value);
                    else
                        m_brightnessValue->setText(m_binaryBrightness
                            ? (value == 0 ? QStringLiteral("关") : QStringLiteral("开"))
                            : QStringLiteral("%1%").arg(value));
                    if (!m_updating)
                        debounce->start();
                });
        connect(debounce, &QTimer::timeout, this, [this, slider, isVolume] {
            if (isVolume)
                emit volumeChangeRequested(slider->value());
            else
                emit brightnessChangeRequested(m_binaryBrightness
                    ? (slider->value() == 0 ? 0 : 100)
                    : slider->value());
        });
    };
    connectSlider(m_volumeSlider, m_volumeValue, true);
    connectSlider(m_brightnessSlider, m_brightnessValue, false);
    connect(m_petStyleButton, &QPushButton::clicked, this, [this] {
        const QString next = m_petStyleButton->text() == QStringLiteral("温和陪伴")
            ? QStringLiteral("活泼陪伴") : QStringLiteral("温和陪伴");
        emit petStyleChangeRequested(next);
    });
    connect(familyButton, &QPushButton::clicked,
            this, &SettingsPage::pairFamilyRequested);

    setSettings({});
    setDeviceSummary({});
}

void SettingsPage::setSettings(const UserSettings& settings)
{
    m_settings = settings;
    m_updating = true;
    m_volumeSlider->setValue(settings.volume);
    updateValueLabel(m_volumeValue, settings.volume);
    updateBrightnessPresentation();
    m_petStyleButton->setText(settings.petStyle);
    m_updating = false;
}

void SettingsPage::setDeviceSummary(const DeviceSummary& summary)
{
    m_soundRow->setSubtitle(summary.audioSummary.isEmpty()
        ? QStringLiteral("未检测到音量控制") : summary.audioSummary);
    m_brightnessRow->setSubtitle(summary.brightnessSummary.isEmpty()
        ? QStringLiteral("未检测到背光控制") : summary.brightnessSummary);
    const bool binaryBrightness = summary.brightnessControlAvailable
        && summary.brightnessLevels == 2;
    if (m_binaryBrightness != binaryBrightness) {
        m_binaryBrightness = binaryBrightness;
        m_updating = true;
        updateBrightnessPresentation();
        m_updating = false;
    }
    m_networkSummary->setText(summary.networkSummary.isEmpty()
        ? QStringLiteral("网络状态未知") : summary.networkSummary);
    m_familyRow->setSubtitle(summary.familySummary.isEmpty()
        ? QStringLiteral("尚未配对") : summary.familySummary);
    m_versionSummary->setText(summary.softwareVersion.isEmpty()
        ? QStringLiteral("LongPet V0.2")
        : QStringLiteral("LongPet V%1").arg(summary.softwareVersion));
    const QString keywordLine = summary.keywordSpottingListening
        ? QStringLiteral("语音：监听中")
        : (summary.keywordSpottingAvailable
            ? QStringLiteral("语音：已就绪")
            : QStringLiteral("语音：未启动"));
    QString visionLine;
    if (summary.visionMonitoring && summary.visionFps > 0.0) {
        visionLine = QStringLiteral("视觉：%1 FPS")
            .arg(summary.visionFps, 0, 'f', 1);
    } else if (summary.visionMonitoring) {
        visionLine = QStringLiteral("视觉：监护中");
    } else if (summary.visionAvailable) {
        visionLine = QStringLiteral("视觉：已就绪");
    } else {
        visionLine = QStringLiteral("视觉：未启动");
    }
    m_keywordSpottingSummary->setText(
        keywordLine + QLatin1Char('\n') + visionLine);
    QStringList perceptionDetails;
    if (!summary.keywordSpottingSummary.isEmpty())
        perceptionDetails.append(summary.keywordSpottingSummary);
    if (!summary.lastKeyword.isEmpty())
        perceptionDetails.append(QStringLiteral("最近关键词：%1").arg(summary.lastKeyword));
    if (!summary.visionSummary.isEmpty())
        perceptionDetails.append(summary.visionSummary);
    m_keywordSpottingSummary->setToolTip(
        perceptionDetails.join(QLatin1Char('\n')));
    m_powerSummary->setText(summary.powerSummary.isEmpty()
        ? QStringLiteral("电源状态未知") : summary.powerSummary);
}

void SettingsPage::updateValueLabel(QLabel* label, int value)
{
    label->setText(QStringLiteral("%1%").arg(value));
}

void SettingsPage::updateBrightnessPresentation()
{
    if (m_binaryBrightness) {
        m_brightnessSlider->setRange(0, 1);
        m_brightnessSlider->setSingleStep(1);
        m_brightnessSlider->setPageStep(1);
        const int value = m_settings.brightness == 0 ? 0 : 1;
        m_brightnessSlider->setValue(value);
        m_brightnessValue->setText(value == 0
            ? QStringLiteral("关") : QStringLiteral("开"));
        return;
    }
    m_brightnessSlider->setRange(0, 100);
    m_brightnessSlider->setSingleStep(1);
    m_brightnessSlider->setPageStep(10);
    m_brightnessSlider->setValue(m_settings.brightness);
    updateValueLabel(m_brightnessValue, m_settings.brightness);
}
