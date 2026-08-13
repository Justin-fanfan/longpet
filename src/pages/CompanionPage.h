#pragma once

#include <QWidget>

class CompanionPage final : public QWidget {
    Q_OBJECT

public:
    explicit CompanionPage(QWidget* parent = nullptr);

signals:
    void controlRequested();
};
