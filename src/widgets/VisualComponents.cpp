#include "VisualComponents.h"

#include "VisualTokens.h"

#include <QDateTime>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QIcon>
#include <QLocale>
#include <QPainter>
#include <QSvgRenderer>
#include <QTimer>
#include <QVBoxLayout>

QLabel* makeLabel(const QString& text, const char* role, QWidget* parent)
{
    auto* label = new QLabel(text, parent);
    label->setProperty("role", role);
    return label;
}

QString weatherIconResource(const QString& conditionCode)
{
    // 和风 icon 编码 → 内置 SVG；未知编码返回空串（纯文本展示）。
    auto resource = [](const char* name) {
        return QStringLiteral(":/icons/weather-%1.svg")
            .arg(QString::fromLatin1(name));
    };
    bool numeric = false;
    const int code = conditionCode.toInt(&numeric);
    if (!numeric)
        return {};
    switch (code) {
    case 100:    // 晴
    case 105:    // 晴转多云（预报场景兜底）
        return resource("sunny");
    case 150:    // 晴（夜间）
        return resource("clear-night");
    case 101:    // 多云
    case 102:    // 少云
    case 103:    // 晴间多云
    case 151:    // 多云（夜间）
    case 152:    // 少云（夜间）
    case 153:    // 晴间多云（夜间）
        return resource("partly-cloudy");
    case 104:    // 阴
    case 154:    // 阴（夜间）
        return resource("cloudy");
    case 302:    // 雷阵雨
    case 303:    // 强雷阵雨
    case 304:    // 雷阵雨伴有冰雹
        return resource("thunderstorm");
    default:
        break;
    }
    if (code >= 300 && code < 400)      // 阵雨 ~ 特大暴雨
        return resource("rain");
    if (code >= 400 && code < 500)      // 小雪 ~ 暴雪
        return resource("snow");
    if (code >= 500 && code < 510)      // 雾 / 霾 / 沙尘
        return resource("fog");
    if (code >= 510 && code < 520)      // 风
        return resource("windy");
    return {};
}

SvgIconWidget::SvgIconWidget(const QString& resourcePath, int size, QWidget* parent)
    : QWidget(parent), m_resourcePath(resourcePath)
{
    setFixedSize(size, size);
    setAttribute(Qt::WA_TranslucentBackground, true);
}

void SvgIconWidget::setResourcePath(const QString& resourcePath)
{
    m_resourcePath = resourcePath;
    update();
}

QString SvgIconWidget::resourcePath() const
{
    return m_resourcePath;
}

void SvgIconWidget::paintEvent(QPaintEvent*)
{
    QSvgRenderer renderer(m_resourcePath);
    if (!renderer.isValid())
        return;
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    renderer.render(&painter, rect());
}

LargeActionButton::LargeActionButton(const QString& text, const QString& iconPath,
                                     const char* role, QWidget* parent)
    : QPushButton(text, parent)
{
    setProperty("role", role);
    setCursor(Qt::PointingHandCursor);
    if (!iconPath.isEmpty()) {
        setIcon(QIcon(iconPath));
        setIconSize(QSize(40, 40));
    }
}

StatusBarWidget::StatusBarWidget(bool showSettings, QWidget* parent)
    : QWidget(parent)
{
    setFixedHeight(LongPetUi::Metrics::StatusBarHeight);
    auto* layout = new QGridLayout(this);
    layout->setContentsMargins(32, 0, 32, 0);
    layout->setHorizontalSpacing(0);
    layout->setColumnStretch(0, 1);
    layout->setColumnStretch(2, 1);

    m_dateLabel = makeLabel({}, "status", this);
    m_timeLabel = makeLabel({}, "statusStrong", this);
    m_timeLabel->setAlignment(Qt::AlignCenter);
    m_timeLabel->setFixedWidth(96);

    auto* rightHost = new QWidget(this);
    auto* right = new QHBoxLayout(rightHost);
    right->setContentsMargins(0, 0, 0, 0);
    right->setSpacing(12);

    m_systemLabel = makeLabel(QStringLiteral("状态待设备接口接入"), "status", rightHost);
    m_systemLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    right->addWidget(m_systemLabel);

    // 天气图标：放在整行摘要最前面（天气永远显示在第一位），仅在有
    // 天气数据且编码可映射时显示；不改变状态栏布局与字号。
    m_weatherIcon = new SvgIconWidget({}, 20, rightHost);
    m_weatherIcon->setObjectName(QStringLiteral("weatherConditionIcon"));
    m_weatherIcon->hide();
    right->insertWidget(0, m_weatherIcon, 0, Qt::AlignVCenter);

    if (showSettings) {
        m_settingsButton = new QPushButton(rightHost);
        m_settingsButton->setObjectName(QStringLiteral("settingsButton"));
        m_settingsButton->setProperty("role", "statusAction");
        m_settingsButton->setFixedSize(LongPetUi::Metrics::StatusBarHeight,
                                       LongPetUi::Metrics::StatusBarHeight);
        m_settingsButton->setIcon(QIcon(QStringLiteral(":/icons/settings.svg")));
        m_settingsButton->setIconSize(QSize(32, 32));
        m_settingsButton->setAccessibleName(QStringLiteral("设置"));
        right->addWidget(m_settingsButton);
        connect(m_settingsButton, &QPushButton::clicked,
                this, &StatusBarWidget::settingsRequested);
    }

    layout->addWidget(m_dateLabel, 0, 0, Qt::AlignLeft | Qt::AlignVCenter);
    layout->addWidget(m_timeLabel, 0, 1, Qt::AlignCenter);
    layout->addWidget(rightHost, 0, 2, Qt::AlignRight | Qt::AlignVCenter);

    SystemStatus initial;
    initial.currentDateTime = QDateTime::currentDateTime();
    setStatus(initial);
}

void StatusBarWidget::setStatus(const SystemStatus& status)
{
    const QDateTime now = status.currentDateTime.isValid()
        ? status.currentDateTime : QDateTime::currentDateTime();
    const QLocale chinese(QLocale::Chinese, QLocale::China);
    m_dateLabel->setText(chinese.toString(now.date(), QStringLiteral("M月d日  dddd")));
    m_timeLabel->setText(now.time().toString(QStringLiteral("HH:mm")));

    QStringList summaries;
    if (!status.networkSummary.isEmpty())
        summaries.append(status.networkSummary);
    else if (status.networkKnown)
        summaries.append(status.networkAvailable ? QStringLiteral("网络正常")
                                                 : QStringLiteral("网络断开"));
    if (status.batteryPercent >= 0)
        summaries.append(QStringLiteral("电量 %1%").arg(status.batteryPercent));
    const bool hasWeather = status.weatherSummary != QStringLiteral("--")
        && !status.weatherSummary.isEmpty();
    if (hasWeather) {
        // 小屏紧凑样式：只显示天气图标 + 温度（weatherSummary 形如 "晴 26°"，
        // 取末段温度）。QWeather 条款要求的来源署名移到了设置页「关于设备」，
        // 状态栏不再展示，避免和网络/电量挤在一行。
        QString weatherText = status.weatherSummary;
        const int separator = weatherText.lastIndexOf(QLatin1Char(' '));
        if (separator >= 0)
            weatherText = weatherText.mid(separator + 1);
        summaries.prepend(weatherText);
        const QString weatherIcon = weatherIconResource(status.weatherConditionCode);
        if (weatherIcon.isEmpty()) {
            m_weatherIcon->hide();
        } else {
            m_weatherIcon->setResourcePath(weatherIcon);
            m_weatherIcon->show();
        }
    } else if (m_weatherIcon->isVisible()) {
        m_weatherIcon->hide();
    }
    m_systemLabel->setText(summaries.isEmpty()
        ? QStringLiteral("状态待设备接口接入") : summaries.join(QStringLiteral(" · ")));
}

PageHeaderWidget::PageHeaderWidget(const QString& title, QWidget* parent)
    : QWidget(parent)
{
    setFixedHeight(LongPetUi::Metrics::HeaderHeight);
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);
    outer->addWidget(new StatusBarWidget(false, this));

    auto* row = new QHBoxLayout;
    row->setContentsMargins(32, 4, 32, 4);
    row->setSpacing(16);
    m_backButton = new QPushButton(this);
    m_backButton->setProperty("role", "back");
    m_backButton->setIcon(QIcon(QStringLiteral(":/icons/back.svg")));
    m_backButton->setIconSize(QSize(34, 34));
    m_backButton->setAccessibleName(QStringLiteral("返回"));
    connect(m_backButton, &QPushButton::clicked,
            this, &PageHeaderWidget::backRequested);
    auto* titleLabel = makeLabel(title, "pageTitle", this);
    row->addWidget(m_backButton);
    row->addWidget(titleLabel);
    row->addStretch();
    outer->addLayout(row);
}

SectionCard::SectionCard(QWidget* parent)
    : QFrame(parent)
{
    setObjectName(QStringLiteral("sectionCard"));
    m_layout = new QVBoxLayout(this);
    m_layout->setContentsMargins(24, 20, 24, 20);
    m_layout->setSpacing(10);
}

QVBoxLayout* SectionCard::contentLayout() const { return m_layout; }

SettingRow::SettingRow(const QString& iconPath, const QString& title,
                       const QString& subtitle, QWidget* control, QWidget* parent)
    : QFrame(parent)
{
    setProperty("card", true);
    setFixedHeight(104);
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(24, 8, 24, 8);
    layout->setSpacing(16);
    layout->addWidget(new SvgIconWidget(iconPath, 38, this));

    auto* texts = new QVBoxLayout;
    texts->setSpacing(2);
    texts->addWidget(makeLabel(title, "body", this));
    m_subtitleLabel = makeLabel(subtitle, "assist", this);
    m_subtitleLabel->setVisible(!subtitle.isEmpty());
    texts->addWidget(m_subtitleLabel);
    layout->addLayout(texts, 1);
    if (control)
        layout->addWidget(control);
}

void SettingRow::setSubtitle(const QString& subtitle)
{
    m_subtitleLabel->setText(subtitle);
    m_subtitleLabel->setVisible(!subtitle.isEmpty());
}

ReminderItem::ReminderItem(const QString& time, const QString& title,
                           const QString& iconPath, ReminderVisualState state,
                           QWidget* parent)
    : QPushButton(parent)
{
    setObjectName(QStringLiteral("reminderItem"));
    setCursor(Qt::PointingHandCursor);
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 24, 0);
    layout->setSpacing(16);

    auto* marker = new QFrame(this);
    marker->setObjectName(QStringLiteral("reminderMarker"));
    marker->setProperty("visualState", state == ReminderVisualState::Completed ? "completed"
        : state == ReminderVisualState::Pending ? "pending"
        : state == ReminderVisualState::Disabled ? "disabled" : "missed");
    marker->setFixedSize(6, 64);
    layout->addWidget(marker);

    auto* timeLabel = makeLabel(time, "body", this);
    timeLabel->setFixedWidth(96);
    timeLabel->setProperty("strong", true);
    layout->addWidget(timeLabel);
    auto* divider = new QFrame(this);
    divider->setObjectName(QStringLiteral("divider"));
    divider->setFixedSize(1, 56);
    layout->addWidget(divider);
    layout->addWidget(new SvgIconWidget(iconPath, 40, this));
    layout->addWidget(makeLabel(title, "body", this), 1);

    const QString stateIcon = state == ReminderVisualState::Completed
        ? QStringLiteral(":/icons/check.svg")
        : state == ReminderVisualState::Pending
            ? QStringLiteral(":/icons/clock.svg")
            : state == ReminderVisualState::Disabled
                ? QStringLiteral(":/icons/clock.svg") : QStringLiteral(":/icons/alert.svg");
    const QString stateText = state == ReminderVisualState::Completed
        ? QStringLiteral("已完成")
        : state == ReminderVisualState::Pending
            ? QStringLiteral("待完成")
            : state == ReminderVisualState::Disabled
                ? QStringLiteral("已停用") : QStringLiteral("已错过");
    const char* stateRole = state == ReminderVisualState::Completed ? "success"
        : state == ReminderVisualState::Pending ? "warning"
        : state == ReminderVisualState::Disabled ? "assist" : "danger";
    layout->addWidget(new SvgIconWidget(stateIcon, 32, this));
    auto* stateLabel = makeLabel(stateText, stateRole, this);
    stateLabel->setFixedWidth(76);
    layout->addWidget(stateLabel);
}

ToastWidget::ToastWidget(QWidget* parent)
    : QLabel(parent)
{
    setObjectName(QStringLiteral("toastWidget"));
    setAlignment(Qt::AlignCenter);
    setFixedSize(520, 72);
    m_hideTimer.setSingleShot(true);
    connect(&m_hideTimer, &QTimer::timeout, this, &QWidget::hide);
    hide();
}

void ToastWidget::showMessage(const QString& message, int durationMs)
{
    setText(message);
    if (parentWidget())
        move((parentWidget()->width() - width()) / 2, parentWidget()->height() - height() - 24);
    show();
    raise();
    m_hideTimer.start(qMax(0, durationMs));
}
