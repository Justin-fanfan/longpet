#pragma once

#include <QColor>

namespace LongPetUi::Colors {
inline const QColor BackgroundPrimary{"#121210"};
inline const QColor BackgroundSecondary{"#191816"};
inline const QColor Surface{"#23211E"};
inline const QColor SurfaceSecondary{"#2B2925"};
inline const QColor Pressed{"#3A352D"};
inline const QColor Disabled{"#777168"};
inline const QColor TextPrimary{"#F6F1E7"};
inline const QColor TextSecondary{"#B8B0A3"};
inline const QColor Accent{"#F2B84B"};
inline const QColor Success{"#82C9A5"};
inline const QColor Warning{"#E4BE72"};
inline const QColor Danger{"#EF7B70"};
inline const QColor Divider{"#3C3934"};
inline const QColor SleepBackground{"#0B0B0A"};
inline const QColor SleepContent{"#8E887E"};
}

namespace LongPetUi::Metrics {
inline constexpr int CanvasWidth = 1024;
inline constexpr int CanvasHeight = 600;
inline constexpr int PageMargin = 32;
inline constexpr int StatusBarHeight = 64;
inline constexpr int HeaderHeight = 160;
inline constexpr int BackButtonSize = 80;
inline constexpr int PrimaryButtonHeight = 96;
inline constexpr int EmergencyButtonHeight = 112;
}
