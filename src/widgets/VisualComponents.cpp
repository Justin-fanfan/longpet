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
    }

    layout->addWidget(m_dateLabel, 0, 0, Qt::AlignLeft | Qt::AlignVCenter);
    layout->addWidget(m_timeLabel, 0, 1, Qt::AlignCenter);
    layout->addWidget(rightHost, 0, 2, Qt::AlignRight | Qt::AlignVCenter);

    m_clockTimer.setInterval(30'000);
    m_clockTimer.setTimerType(Qt::VeryCoarseTimer);
    connect(&m_clockTimer, &QTimer::timeout,
            this, &StatusBarWidget::updateDateTime);
    updateDateTime();
    m_clockTimer.start();
}

QPushButton* StatusBarWidget::settingsButton() const { return m_settingsButton; }

void StatusBarWidget::updateDateTime()
{
    const QDateTime now = QDateTime::currentDateTime();
    const QLocale chinese(QLocale::Chinese, QLocale::China);
    m_dateLabel->setText(chinese.toString(now.date(), QStringLiteral("M月d日  dddd")));
    m_timeLabel->setText(now.time().toString(QStringLiteral("HH:mm")));
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
    auto* titleLabel = makeLabel(title, "pageTitle", this);
    row->addWidget(m_backButton);
    row->addWidget(titleLabel);
    row->addStretch();
    outer->addLayout(row);
}

QPushButton* PageHeaderWidget::backButton() const { return m_backButton; }

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
    if (!subtitle.isEmpty())
        texts->addWidget(makeLabel(subtitle, "assist", this));
    layout->addLayout(texts, 1);
    if (control)
        layout->addWidget(control);
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
        : state == ReminderVisualState::Pending ? "pending" : "missed");
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
            ? QStringLiteral(":/icons/clock.svg") : QStringLiteral(":/icons/alert.svg");
    const QString stateText = state == ReminderVisualState::Completed
        ? QStringLiteral("已完成")
        : state == ReminderVisualState::Pending
            ? QStringLiteral("待完成") : QStringLiteral("已错过");
    const char* stateRole = state == ReminderVisualState::Completed ? "success"
        : state == ReminderVisualState::Pending ? "warning" : "danger";
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
