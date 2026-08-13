# LongPet V0.1

LongPet V0.1 是面向龙芯 2K0300 宠物终端的正式 Qt 6 Widgets UI 骨架。它从 `longpetui_2` 视觉原型提取经过验证的视觉组件，但不包含 Demo 导航、假业务数据或尚未接入的 AI/硬件能力。

## V0.1 功能范围

- 1024×600 正式产品画布；
- 低 CPU 的 `DefaultOpen` 宠物待机界面；
- 整屏触摸进入控制页；
- 语义化页面信号，不向外暴露按钮控件；
- 15 秒无操作后自动返回待机页；
- 未接入能力使用非模态 Toast 明确说明；
- Windows 窗口预览，Linux/2K0300 全屏运行；
- QRC 内嵌 QSS 和 SVG，无本机绝对资源路径。

语音、关怀、提醒、设置、SQLite、视觉、运动和远端通信不属于 V0.1。它们按照整体架构路线在后续版本逐步接入。

## Windows 构建

需要 Qt 6.5+（Core、Gui、Widgets、Svg）、CMake 3.21+ 和与 Qt 匹配的 C++17 工具链。

```powershell
cmake -S . -B build -G Ninja `
  -DCMAKE_BUILD_TYPE=Release `
  -DCMAKE_PREFIX_PATH=D:/Qt/6.11.0/mingw_64 `
  -DCMAKE_MAKE_PROGRAM=D:/Qt/Tools/Ninja/ninja.exe `
  -DCMAKE_CXX_COMPILER=D:/Qt/Tools/mingw1310_64/bin/g++.exe
cmake --build build
```

## LoongArch64 交叉构建

默认 SDK 路径为 `/opt/loongarch64-buildroot-linux-gnu_sdk-buildroot`。可通过 `LOONGARCH_SDK_ROOT` 覆盖。

```bash
./scripts/build-loongarch.sh
```

板端运行环境需设置与镜像匹配的 `QT_QPA_PLATFORM=linuxfb` 和 `TSLIB_*` 参数。实际 CPU、触摸和长稳指标必须在 2K0300 Release 环境验收。

## 项目结构

```text
src/main.cpp
src/mainwindow.*
src/pages/CompanionPage.*
src/pages/HomePage.*
src/pages/ConversationPage.*
src/pages/CarePage.*
src/pages/ReminderPage.*
src/pages/ReminderEditPage.*
src/pages/SettingsPage.*
src/pages/EmergencyPage.*
src/pages/SleepPage.*
src/widgets/PetFaceWidget.*
src/widgets/VisualComponents.*
src/widgets/VisualTokens.h
resources/
cmake/toolchains/
docs/
```

V0.1 运行主路径只实例化 `CompanionPage` 与 `HomePage`。其余产品页面已经作为后续版本的设计资产迁入并持续参加编译/渲染测试，但在对应业务 Service 和语义信号改造完成前不进入正式导航。

