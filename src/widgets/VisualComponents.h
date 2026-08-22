#pragma once

#include "model/SystemModels.h"

#include <QFrame>
#include <QLabel>
#include <QPushButton>
#include <QTimer>

class QBoxLayout;
class QSlider;
class QSvgRenderer;
class QVBoxLayout;

QLabel* makeLabel(const QString& text, const char* role, QWidget* parent = nullptr);

class SvgIconWidget final : public QWidget {
public:
    explicit SvgIconWidget(const QString& resourcePath, int size = 36, QWidget* parent = nullptr);
    void setResourcePath(const QString& resourcePath);

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    QString m_resourcePath;
};

class LargeActionButton final : public QPushButton {
public:
    explicit LargeActionButton(const QString& text, const QString& iconPath,
                               const char* role, QWidget* parent = nullptr);
};

class StatusBarWidget final : public QWidget {
    Q_OBJECT

public:
    explicit StatusBarWidget(bool showSettings = false, QWidget* parent = nullptr);
    void setStatus(const SystemStatus& status);

signals:
    void settingsRequested();

private:
    QLabel* m_dateLabel = nullptr;
    QLabel* m_timeLabel = nullptr;
    QLabel* m_systemLabel = nullptr;
    QPushButton* m_settingsButton = nullptr;
};

class PageHeaderWidget final : public QWidget {
    Q_OBJECT

public:
    explicit PageHeaderWidget(const QString& title, QWidget* parent = nullptr);

signals:
    void backRequested();

private:
    QPushButton* m_backButton = nullptr;
};

class SectionCard final : public QFrame {
public:
    explicit SectionCard(QWidget* parent = nullptr);
    QVBoxLayout* contentLayout() const;

private:
    QVBoxLayout* m_layout = nullptr;
};

class SettingRow final : public QFrame {
public:
    explicit SettingRow(const QString& iconPath, const QString& title,
                        const QString& subtitle, QWidget* control,
                        QWidget* parent = nullptr);
    void setSubtitle(const QString& subtitle);

private:
    QLabel* m_subtitleLabel = nullptr;
};

enum class ReminderVisualState {
    Completed,
    Acknowledged,
    Presented,
    Pending,
    Missed,
    Disabled
};

class ReminderItem final : public QPushButton {
public:
    explicit ReminderItem(const QString& time, const QString& title,
                          const QString& iconPath, ReminderVisualState state,
                          QWidget* parent = nullptr);
};

class ToastWidget final : public QLabel {
public:
    explicit ToastWidget(QWidget* parent);
    void showMessage(const QString& message, int durationMs = 2600);

private:
    QTimer m_hideTimer;
};
