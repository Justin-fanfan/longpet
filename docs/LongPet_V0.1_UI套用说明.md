# LongPet V0.1 UI 套用说明

> 适用 UI 原型：`Justin-fanfan/longpetui_2`
> 正式项目：LongPet
> 目标平台：Windows Qt Creator 调试 + WSL/Buildroot LoongArch64 Release + 龙芯 2K0300
> 显示设备：7 英寸、1024×600、横屏触摸
> GUI：Qt 6 Widgets
> 版本：V0.1

---

## 1. 套用原则

当前 `longpetui_2` 应当被理解为：

> **LongPet 的 UI 设计源、视觉组件库和页面原型。**

而不是：

> **直接复制后继续向里面堆业务的正式工程架构。**

正式 V0.1 推荐：

1. 保留已经验证过的视觉组件；
2. 保留 QSS、图标、PetFace 绘制；
3. 保留 Companion/Home 页视觉；
4. 去掉 Demo 专属代码；
5. 新建正式 `MainWindow`；
6. V0.1 只加入真正需要的页面；
7. 页面通过 signal 表达用户意图；
8. 暂时不引入复杂 Controller/Service；
9. 后续版本再逐步增加业务层。

---

# 2. 当前 UI 原型中哪些东西可以直接复用

## 2.1 `PetFaceWidget`

建议：

**直接复用当前优化版本。**

文件：

```text
src/widgets/PetFaceWidget.h
src/widgets/PetFaceWidget.cpp
```

它应该继续只承担：

```text
PetExpression
    ↓
宠物脸视觉
```

当前表情包括：

```text
Default
DefaultOpen
Playful
Happy
Worried
Angry
Sleep
Listening
Thinking
Speaking
Alert
CuteCat
```

同时保留当前性能优化：

* 静态 `QImage` Cache；
* 低频动态动画；
* one-shot blink；
* hidden 时停止 Timer；
* dirty-region repaint；
* `WA_OpaquePaintEvent`；
* Listening / Thinking / Speaking 独立低刷新率。

正式工程不要重新加入：

```text
全脸 breathing
12 FPS 总时钟
所有表情持续 repaint
```

---

# 3. `VisualComponents` 可以直接复用

文件：

```text
src/widgets/VisualComponents.h
src/widgets/VisualComponents.cpp
```

当前组件：

```text
makeLabel()
SvgIconWidget
LargeActionButton
StatusBarWidget
PageHeaderWidget
SectionCard
SettingRow
ReminderItem
ToastWidget
```

这些都是纯视觉组件。

---

## 3.1 `makeLabel`

用于统一创建：

```text
标题
正文
辅助文字
状态文字
```

并通过：

```cpp
role
```

交给 QSS 控制。

页面中尽量继续：

```cpp
makeLabel(...)
```

而不是每次手动：

```cpp
QLabel*
font
color
size
```

---

## 3.2 `SvgIconWidget`

职责：

```text
SVG resource
    ↓
轻量 Qt 图标显示
```

它不应该：

* 联网；
* 判断状态；
* 加载业务数据。

---

## 3.3 `LargeActionButton`

用于：

```text
陪我说话
今日关怀
提醒
```

等大尺寸适老触摸按钮。

主要统一：

* 高度；
* 图标；
* 字号；
* role；
* pressed 状态。

---

## 3.4 `StatusBarWidget`

负责状态栏视觉。

目前可以继续显示：

```text
日期
时间
天气
设置入口
```

正式版后续真实天气数据不要由它自己联网获取。

未来采用：

```cpp
statusBar->setWeather(...);
```

这样的数据输入方式即可。

---

## 3.5 `PageHeaderWidget`

用于统一：

```text
返回按钮 + 页面标题
```

Care、Reminder、Settings 等普通二级页面都优先复用。

---

## 3.6 `SectionCard`

负责统一卡片容器。

例如：

```text
喝水
吃药
活动
设置区域
```

它只是视觉容器。

不要给 `SectionCard` 加业务职责。

---

## 3.7 `SettingRow`

负责：

```text
Icon | Title / Subtitle | Control
```

例如：

```text
🔊 声音大小        Slider
```

---

## 3.8 `ReminderItem`

负责提醒列表一项的显示：

```text
时间
标题
图标
Completed
Pending
Missed
```

不负责真正执行 Reminder。

---

## 3.9 `ToastWidget`

建议正式 V0.1 直接使用。

例如：

```cpp
m_toast->showMessage(
    QStringLiteral("语音功能将在下一版本接入")
);
```

不要使用：

```cpp
QMessageBox
```

作为老人主界面提示。

---

# 4. `VisualTokens` 直接复用

文件：

```text
src/widgets/VisualTokens.h
```

当前已经定义：

```text
Colors
Metrics
```

例如：

```text
CanvasWidth = 1024
CanvasHeight = 600
PageMargin = 32
StatusBarHeight = 64
PrimaryButtonHeight = 96
```

以后如果发现很多页面都出现同一个固定数值，应优先考虑进入 Token。

不要逐渐变成：

```cpp
页面 A：按钮高 95
页面 B：按钮高 97
页面 C：按钮高 92
```

---

# 5. `app.qss` 直接复用

建议正式工程继续使用：

```text
resources/styles/app.qss
```

让它负责：

```text
字体
颜色
Button
Card
Pressed
Disabled
Danger
Slider
Toast
```

不要把视觉重新散落进：

```cpp
widget->setStyleSheet(...)
```

只有真正动态、QSS 很难表达的绘制才放 C++。

---

# 6. resources 直接复制

推荐：

```text
resources/
├── resources.qrc
├── styles/
│   └── app.qss
├── icons/
└── 其他视觉资源
```

继续统一通过：

```text
:/icons/xxx.svg
```

访问。

禁止正式项目出现：

```text
C:\Users\...
/root/...
/home/...
```

这样的运行资源路径。

---

# 7. 哪些东西不要直接复制

最重要的是：

```text
DemoWindow
```

不要直接改名成：

```text
MainWindow
```

然后继续用。

当前 `DemoWindow` 是 UI Prototype 的运行外壳。

它包含：

```text
所有页面创建
Demo 导航
12 秒 Timeout
F1～F12
截图系统
Gallery
Engineering Demo
假的 Reminder 保存
假的 Emergency 行为
```

正式项目需要一个更干净的顶层。

---

# 8. 正式 V0.1 使用 QWidget 顶层

推荐：

```cpp
class MainWindow final : public QWidget
```

而不是：

```cpp
class MainWindow : public QMainWindow
```

因为 LongPet 没有：

```text
MenuBar
ToolBar
DockWidget
Desktop StatusBar
```

等需求。

正式结构：

```text
QApplication
      │
      ▼
MainWindow : QWidget
      │
      └── QStackedWidget
             ├── CompanionPage
             └── HomePage
```

---

# 9. V0.1 MainWindow 做什么

V0.1 的 `MainWindow` 只负责：

```text
1024×600
页面容器
Companion ↔ Home
Control timeout
全局 Toast
```

例如：

```cpp
class MainWindow final : public QWidget
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);

private:
    void showCompanion();
    void showHome();
    void restartControlTimeout();

    QStackedWidget* m_stack = nullptr;
    QTimer* m_controlTimeout = nullptr;

    CompanionPage* m_companionPage = nullptr;
    HomePage* m_homePage = nullptr;

    ToastWidget* m_toast = nullptr;
};
```

不要现在创建：

```text
NavigationService
Router
PageManager
ScreenManager
```

---

# 10. CompanionPage 怎么套

当前视觉布局可以直接复用。

但正式 Companion 不应该默认使用：

```cpp
PetExpression::Speaking
```

推荐：

```cpp
PetExpression::DefaultOpen
```

或者：

```cpp
PetExpression::Default
```

正常待机：

```text
DefaultOpen
    ↓
平时 0 FPS
    ↓
几秒一次 blink
```

这非常适合机器宠物长期待机。

---

# 11. CompanionPage 接口需要调整

UI Prototype 当前是：

```cpp
QPushButton* revealButton() const;
```

正式项目建议改成：

```cpp
class CompanionPage final : public QWidget
{
    Q_OBJECT

signals:
    void controlRequested();
};
```

页面内部：

```cpp
connect(m_revealButton,
        &QPushButton::clicked,
        this,
        &CompanionPage::controlRequested);
```

MainWindow 只处理：

```text
controlRequested
```

而不需要知道：

```text
页面里具体是哪一个 QPushButton
```

---

# 12. HomePage 怎么套

当前视觉布局继续复用。

正式接口建议从：

```cpp
talkButton()
careButton()
reminderButton()
settingsButton()
```

改成：

```cpp
signals:
    void talkRequested();
    void careRequested();
    void reminderRequested();
    void settingsRequested();
```

这是 V0.1 最值得做的一次小型改造。

---

# 13. 为什么 V0.1 就应该做 signal

因为以后：

```text
HomePage
```

不应该知道：

```text
VoiceService
ReminderService
CareService
```

它只负责表达：

```text
用户点了“陪我说话”
```

因此：

```text
HomePage
    ↓ talkRequested()
MainWindow / AppController
```

比：

```text
外部拿 talkButton()
再 connect()
```

更适合正式工程。

---

# 14. V0.1 的实际功能范围

当前 UI Prototype 的页面很多，但正式 V0.1 不建议全部迁入。

推荐第一阶段只加入：

```text
CompanionPage
HomePage
ToastWidget
```

其他功能使用 Toast。

例如：

```text
陪我说话
→ 语音功能将在下一版本接入

今日关怀
→ 今日关怀功能将在下一版本接入

提醒
→ 提醒功能将在下一版本接入
```

这样：

* UI 产品感已经存在；
* 不会把假业务误认为正式能力；
* V0.1 边界很干净。

---

# 15. 如果比赛需要看完整 UI

可以增加编译期开关：

```cmake
option(
    LONGPET_ENABLE_UI_DEMO
    "Enable UI-only demo pages"
    OFF
)
```

Windows：

```text
ON
```

可以进入：

```text
Care
Reminder
Settings
Emergency
Sleep
```

正式板端 V0.1：

```text
OFF
```

只出现已经完成的功能。

---

# 16. V0.1 推荐目录

```text
LongPet/
├── CMakeLists.txt
├── README.md
│
├── cmake/
│   └── toolchains/
│       └── loongarch64-buildroot.cmake
│
├── src/
│   ├── main.cpp
│   ├── MainWindow.cpp
│   ├── MainWindow.h
│   │
│   ├── pages/
│   │   ├── CompanionPage.cpp
│   │   ├── CompanionPage.h
│   │   ├── HomePage.cpp
│   │   └── HomePage.h
│   │
│   └── widgets/
│       ├── PetFaceWidget.cpp
│       ├── PetFaceWidget.h
│       ├── VisualComponents.cpp
│       ├── VisualComponents.h
│       └── VisualTokens.h
│
├── resources/
│   ├── resources.qrc
│   ├── styles/
│   │   └── app.qss
│   └── icons/
│
└── docs/
```

---

# 17. V0.1 CMake

推荐：

```cmake
cmake_minimum_required(VERSION 3.21)

project(
    LongPet
    VERSION 0.1.0
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

find_package(
    Qt6 6.5
    REQUIRED
    COMPONENTS
        Core
        Gui
        Widgets
        Svg
)

qt_add_executable(
    LongPet

    src/main.cpp

    src/MainWindow.cpp
    src/MainWindow.h

    src/pages/CompanionPage.cpp
    src/pages/CompanionPage.h

    src/pages/HomePage.cpp
    src/pages/HomePage.h

    src/widgets/PetFaceWidget.cpp
    src/widgets/PetFaceWidget.h

    src/widgets/VisualComponents.cpp
    src/widgets/VisualComponents.h

    src/widgets/VisualTokens.h

    resources/resources.qrc
)

target_include_directories(
    LongPet
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(
    LongPet
    PRIVATE
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets
        Qt6::Svg
)
```

V0.1：

> 保持一个 executable、一个根 CMakeLists。

---

# 18. LoongArch Toolchain

当前 UI 仓库中的：

```text
cmake/toolchains/loongarch64-buildroot.cmake
```

建议直接复制。

当前环境为：

```text
SDK_ROOT
/opt/loongarch64-buildroot-linux-gnu_sdk-buildroot
```

实际编译器：

```text
loongarch64-loongson-linux-gnu-gcc
loongarch64-loongson-linux-gnu-g++
```

目标 sysroot：

```text
loongarch64-buildroot-linux-gnu/sysroot
```

继续保留：

```cmake
QT_HOST_PATH
Qt6_DIR
CMAKE_FIND_ROOT_PATH
```

等现有配置。

---

# 19. Windows Debug

建议 Qt Creator Kit：

```text
LongPet Windows Debug
```

Build：

```text
Debug
```

构建目录：

```text
C:\build\LongPet\win-debug
```

用途：

```text
UI
signal/slot
业务逻辑
鼠标模拟触摸
断点调试
```

---

# 20. LoongArch Release

构建目录：

```text
~/build/LongPet-loong-release
```

例如：

```sh
cmake \
    -S . \
    -B ~/build/LongPet-loong-release \
    -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_TOOLCHAIN_FILE="$PWD/cmake/toolchains/loongarch64-buildroot.cmake"

cmake \
    --build ~/build/LongPet-loong-release \
    -j$(nproc)
```

验证：

```sh
file ~/build/LongPet-loong-release/LongPet
```

必须确认是：

```text
LoongArch ELF
```

---

# 21. main.cpp

正式版去掉：

```text
captureAllPages
--capture-dir
Demo application name
```

推荐：

```cpp
#include <QApplication>
#include <QFile>

#include "MainWindow.h"

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);

    QFile styleFile(QStringLiteral(":/styles/app.qss"));

    if (styleFile.open(
            QIODevice::ReadOnly |
            QIODevice::Text)) {
        app.setStyleSheet(
            QString::fromUtf8(
                styleFile.readAll()
            )
        );
    }

    MainWindow window;

#ifdef Q_OS_WIN

    window.setFixedSize(1024, 600);
    window.show();

#else

    window.showFullScreen();

#endif

    return app.exec();
}
```

---

# 22. V0.1 页面流程

建议正式流程：

```text
程序启动
   ↓
CompanionPage
   ↓
用户触屏
   ↓
HomePage
   ↓
用户操作
   ├── talk → Toast
   ├── care → Toast
   ├── reminder → Toast
   └── settings → Toast
   ↓
12～15 秒无操作
   ↓
CompanionPage
```

---

# 23. 性能要求

保留当前优化版 `PetFaceWidget`。

建议内部目标：

| 页面                |  UI CPU |
| ----------------- | ------: |
| Companion Default |     <5% |
| Home              |     <5% |
| Listening         |    <15% |
| Thinking          | <10～15% |
| Speaking          |    <15% |

Speaking 短时达到约 15% 可以接受。

但正式 Companion 不应该长期运行 Speaking。

---

# 24. V0.1 不合入的 Demo 内容

正式工程不要合入：

```text
DemoWindow
UiGalleryPage
captureAllPages()
F1～F12 shortcuts
截图 manifest
Demo navigation
假的 CPU/RAM/FPS
假的提醒保存
假的家属联系
```

---

# 25. 页面迁移优先级

## V0.1

迁入：

```text
CompanionPage
HomePage
```

## 后续

再按需要迁入：

```text
ConversationPage
CarePage
ReminderPage
ReminderEditPage
SettingsPage
EmergencyPage
SleepPage
```

不要因为代码已经写好了，就一次性把所有页面塞进正式应用。

---

# 26. V0.1 最终结构

```text
MainWindow
│
├── CompanionPage
│      └── PetFaceWidget
│
├── HomePage
│      ├── StatusBarWidget
│      ├── PetFaceWidget
│      └── LargeActionButton
│
└── ToastWidget
```

依赖关系：

```text
MainWindow
    ↓
Pages
    ↓
Visual Widgets
```

---

# 27. V0.1 最重要的代码原则

页面只负责：

```text
显示
+
用户输入
```

不要出现：

```cpp
HomePage::startAsr();
HomePage::openCamera();
ReminderPage::writeJson();
SettingsPage::setSystemVolume();
```

---

# 28. 推荐迁移顺序

1. 新建正式 LongPet 工程；
2. 复制 toolchain；
3. 复制 resources；
4. 复制 VisualTokens；
5. 复制 VisualComponents；
6. 复制优化后的 PetFaceWidget；
7. Windows 编译；
8. LoongArch 编译；
9. 加 CompanionPage；
10. 加 HomePage；
11. 把按钮 getter 改 signal；
12. 实现 Companion ↔ Home；
13. 加 Timeout；
14. 加 Toast；
15. 板端测试；
16. 做 V0.1 Release。

---

# 29. V0.1 Definition of Done

* [ ] Windows Debug 正常；
* [ ] LoongArch Release 正常；
* [ ] 1024×600；
* [ ] linuxfb 全屏；
* [ ] tslib 触摸正常；
* [ ] Companion 默认脸正常；
* [ ] Home 正常；
* [ ] Toast 正常；
* [ ] Control timeout 正常；
* [ ] 静态 UI CPU <5% 左右；
* [ ] 无未实现功能伪装成真实数据；
* [ ] DemoWindow 未进入正式运行架构；
* [ ] 页面没有直接调用 AI/硬件；
* [ ] 运行 2 小时无崩溃。

---

# 30. 一句话原则

> **把 `longpetui_2` 当作视觉组件库和页面设计源，而不是正式应用框架；V0.1 优先复用 `PetFaceWidget + VisualComponents + VisualTokens + QSS + CompanionPage + HomePage`，用新的 `MainWindow : QWidget` 作为正式产品外壳。**
