#include "SettingsPage.h"

#include "widgets/VisualComponents.h"

#include <QCheckBox>
#include <QGridLayout>
#include <QIcon>
#include <QLabel>
#include <QPushButton>
#include <QSlider>
#include <QVBoxLayout>

namespace {
QWidget* sliderControl(int value, QWidget* parent)
{
    auto* box = new QWidget(parent);
    box->setFixedWidth(220);
    auto* layout = new QVBoxLayout(box);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    auto* valueLabel = makeLabel(QString::number(value) + QStringLiteral("%"), "assist", box);
    valueLabel->setAlignment(Qt::AlignRight);
    auto* slider = new QSlider(Qt::Horizontal, box);
    slider->setRange(0, 100);
    slider->setValue(value);
    slider->setFixedHeight(64);
    QObject::connect(slider, &QSlider::valueChanged, valueLabel, [valueLabel](int v) {
        valueLabel->setText(QString::number(v) + QStringLiteral("%"));
    });
    layout->addWidget(valueLabel);
    layout->addWidget(slider);
    return box;
}

QPushButton* chevronButton(QWidget* parent)
{
    auto* button = new QPushButton(parent);
    button->setFixedSize(64, 64);
    button->setIcon(QIcon(QStringLiteral(":/icons/chevron.svg")));
    button->setIconSize(QSize(30, 30));
    button->setAccessibleName(QStringLiteral("查看"));
    return button;
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
    m_backButton = header->backButton();
    root->addWidget(header);

    auto* grid = new QGridLayout;
    grid->setContentsMargins(32, 16, 32, 32);
    grid->setHorizontalSpacing(32);
    grid->setVerticalSpacing(16);
    auto* sound = new SettingRow(QStringLiteral(":/icons/volume.svg"), QStringLiteral("声音大小"),
        QStringLiteral("演示数值"), sliderControl(60, this), this);
    auto* brightness = new SettingRow(QStringLiteral(":/icons/brightness.svg"), QStringLiteral("屏幕亮度"),
        QStringLiteral("演示数值"), sliderControl(72, this), this);

    auto* networkSwitch = new QCheckBox(QStringLiteral("已连接"), this);
    networkSwitch->setChecked(true);
    networkSwitch->setFixedHeight(64);
    connect(networkSwitch, &QCheckBox::toggled, networkSwitch, [networkSwitch](bool checked) {
        networkSwitch->setText(checked ? QStringLiteral("已连接") : QStringLiteral("未连接"));
    });
    auto* network = new SettingRow(QStringLiteral(":/icons/network.svg"), QStringLiteral("网络连接"),
        QStringLiteral("仅改变视觉状态"), networkSwitch, this);

    auto* familyButton = chevronButton(this);
    auto* family = new SettingRow(QStringLiteral(":/icons/family.svg"), QStringLiteral("家人联系方式"),
        QStringLiteral("查看演示信息"), familyButton, this);
    auto* petButton = chevronButton(this);
    auto* pet = new SettingRow(QStringLiteral(":/icons/pet.svg"), QStringLiteral("宠物设置"),
        QStringLiteral("表情与陪伴风格"), petButton, this);
    auto* aboutButton = chevronButton(this);
    auto* about = new SettingRow(QStringLiteral(":/icons/info.svg"), QStringLiteral("关于设备"),
        QStringLiteral("LongPet UI Prototype"), aboutButton, this);

    grid->addWidget(sound, 0, 0);
    grid->addWidget(brightness, 0, 1);
    grid->addWidget(network, 1, 0);
    grid->addWidget(family, 1, 1);
    grid->addWidget(pet, 2, 0);
    grid->addWidget(about, 2, 1);
    grid->setRowStretch(3, 1);
    root->addLayout(grid, 1);

    m_toast = new ToastWidget(this);
    const auto showDemo = [this] { m_toast->showMessage(QStringLiteral("仅演示界面，不会读取或修改设备")); };
    connect(familyButton, &QPushButton::clicked, this, showDemo);
    connect(petButton, &QPushButton::clicked, this, showDemo);
    connect(aboutButton, &QPushButton::clicked, this, showDemo);
}

QPushButton* SettingsPage::backButton() const { return m_backButton; }
