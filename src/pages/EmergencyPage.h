#pragma once

#include <QString>
#include <QWidget>

class QPushButton;
class QLabel;
class ToastWidget;

class EmergencyPage final : public QWidget {
    Q_OBJECT

public:
    explicit EmergencyPage(QWidget* parent = nullptr);
    QPushButton* okayButton() const;
    QPushButton* contactButton() const;
    ToastWidget* toast() const;
    void setDetail(const QString& detail);
    QString detail() const;

signals:
    void dismissRequested();
    void contactRequested();

private:
    QPushButton* m_okayButton = nullptr;
    QPushButton* m_contactButton = nullptr;
    QLabel* m_detailLabel = nullptr;
    ToastWidget* m_toast = nullptr;
};
