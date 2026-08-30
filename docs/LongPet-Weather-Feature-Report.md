# LongPet 天气功能整体报告

- 日期：2026-08-30
- 范围：阶段一（天气功能完整实现）+ 阶段二（QWeather 官方 API 修正检查）+ 阶段三（上板实测修正与状态栏图标）+ 阶段三补充（状态栏紧凑化、署名移位）
- 数据来源：和风天气（QWeather），专属 API Host 版本 `/weather/v1/current/{latitude}/{longitude}`
- 结论：**功能已完成；阶段二 6 项修正全部落地；上板实测两处根因均已定位并修复（数值型 JSON 字段解析 + 启动竞态离线不重试）；状态栏天气为「图标 + 温度」紧凑样式，来源署名移至设置页「关于设备」；自动测试 31 通过 / 0 失败 / 1 跳过；Windows 与 LoongArch 交叉构建均通过（零警告）**

---

## 1. 需求与结论概述

LongPet 需要两个天气能力：

1. **状态栏真实天气**：小屏顶部状态栏显示如 `[图标] 26°`，数据有刷新周期、过期标记、离线降级；
2. **语音注入 LLM 上下文**：用户问「现在天气怎么样」时，把缓存天气快照作为事实上下文注入模型消息，降低幻觉。

硬约束：

- 不重构现有 UI / AI / Family Link / 视频通话 / USB 音频体系；
- 天气配置完全独立于 AI 配置（独立文件、独立环境变量）；
- 不与任何 Provider 耦合——沿用既有 Asr/Llm/Tts Provider 端口 + 工厂 + Service 分层模式；
- 相控要求：产品必须展示数据来源署名（QWeather 使用条款）。

---

## 2. 整体架构

```
                     ┌────────────────────────────────────────────┐
 data/               │   WeatherConfigRepository (.ini + env 覆盖)  │
 WeatherConfig       │   → WeatherConfiguration                     │
 Repository          └────────────────────────────────────────────┘
                                            │
                    WeatherProviderFactory ─┘ create(配置)
                                            │
 services/           ┌──────────────────────┴───────────────────────┐
 WeatherPorts        │  WeatherProviderPort (抽象, QObject)          │
                     │    fetchCurrent() / cancel()                 │
                     │    currentWeatherReady(snapshot)             │
                     │    fetchFailed(error)                        │
                     └──────────────────────┬───────────────────────┘
                                            │
 platform/          ┌───────────────────────▼───────────────────┐
 QWeatherProvider   │  QWeatherProvider (和风 API 实现)          │
                    └───────────────────────┬───────────────────┘
                                            │
 services/          ┌───────────────────────▼───────────────────┐
 WeatherService     │  refresh 定时 / 缓存 / stale / 离线降级     │
                    │  → SystemService::setWeatherSummary       │
                    └───────────────────────┬───────────────────┘
                                            │
                    setWeatherProvider(λ) ──▼──► VoiceInteractionService
                    只读 std::function          messagesFor() 注入 system 上下文
```

- **端口抽象**：`WeatherProviderPort` 与 `AsrProviderPort / LlmProviderPort / TtsProviderPort` 同构，业务层不感知厂商；
- **组合根**：`Application` 创建仓库 → 加载配置 → 工厂建 Provider → 组装 `WeatherService` → 给语音服务注入口；
- **决策分离**：`VoiceInteractionService` 只拿到 `std::function<std::optional<WeatherSnapshot>()>`，不触网、不拿 Key，构造签名未改（既有调用点零改动）。

### 2.1 数据模型（`src/model/WeatherModels.h`）

```cpp
struct WeatherSnapshot {
    bool   valid = false;
    QString condition;       // 天气现象文本, 如 多云
    QString conditionCode;   // 和风 icon 编码(为动态图标预留)
    double temperatureC;     // 温度 ℃
    double feelsLikeC;       // 体感 ℃
    int    humidityPercent;  // 湿度百分比 0-100 (API 0.69 → 69)
    double latitude, longitude;
    QDateTime observedAt;    // 服务端观测时间; 新版 API 未提供时保持 invalid
    QDateTime updatedAt;     // 本机最近一次成功获取响应的 UTC 时间
    bool   stale = false;    // 超过 stale_after_minutes 后置位
    QString summary() const; // "多云 24°"
};

struct WeatherConfiguration {
    bool enabled; QString provider, apiHost, apiKey;
    double latitude, longitude;
    QString language = "zh";
    int refreshMinutes = 30, requestTimeoutMs = 10000, staleAfterMinutes = 120;
    // validationError() / isValid() / hasValidLocation()
};

enum class WeatherErrorCode { Disabled, ConfigurationError, NetworkUnavailable,
    NetworkError, Timeout, Unauthorized, RateLimited, ServerError,
    InvalidResponse, EmptyResult, Cancelled };
```

### 2.2 配置（独立于 AI 配置）

- 路径：`/etc/longpet/longpet-weather.ini`（Linux），Windows 在 AppConfig 目录；
- 权限：`root:longpet 0640`；`longpet.service` 通过 `LONGPET_WEATHER_CONFIG` 指定；
- 优先级：**环境变量 > INI > 默认值**；
- 示例：[deploy/longpet-weather.ini.example](../deploy/longpet-weather.ini.example)；
- 环境变量：`LONGPET_WEATHER_{CONFIG,ENABLED,PROVIDER,API_HOST,API_KEY,LATITUDE,LONGITUDE,LANGUAGE,REFRESH_MINUTES,REQUEST_TIMEOUT_MS,STALE_AFTER_MINUTES}`；
- 安全：API Key 只经 `X-QW-Api-Key` 请求头上传，不进 URL、不写日志、不硬编码。

### 2.3 状态栏集成

- `WeatherService` 成功刷新 → `SystemService::setWeatherSummary(summary, conditionCode)`；
- 状态栏（[VisualComponents.cpp](../src/widgets/VisualComponents.cpp)）展示：

```text
[晴图标] 26° · 网络正常 · 电量 80%
```

- 图标：和风 `icon` 编码经 `weatherIconResource()` 映射为内置 SVG（100→晴，150→晴夜，
  101/102/103/151-153→多云，104/154→阴，302-304→雷阵雨，3xx→雨，4xx→雪，5xx→雾霾沙尘，51x→风），
  未知编码保持纯文本；图标仅在天气数据有效时显示，`SvgIconWidget` 固定 20px 内嵌于摘要行首，
  不改变状态栏布局与字号（[VisualComponents.cpp:127-135](../src/widgets/VisualComponents.cpp#L127)）；
- 未启用/从未成功时保持 `--`，不写错任何现有逻辑；
- 紧凑样式只留图标 + 温度（`weatherSummary` 形如 "晴 26°"，取末段"26°"）；QWeather 条款要求的
  来源署名移至设置页「关于设备」（见 §4.5）。

### 2.4 语音注入

- [VoiceInteractionService.cpp](../src/services/VoiceInteractionService.cpp) `messagesFor()` 在 provider 返回有效快照时注入一条 system 上下文：天气/温度/体感/湿度/更新时间（UTC→本地显示）/过期提示，并注明"不包含未来天气预报，不要自行猜测"；
- 过期数据会明确提示"可能已经过期"，避免模型把旧天当新天。

---

## 3. 阶段二：QWeather 官方 API 修正检查（6 项）

### 3.1 部署示例不再使用 `devapi.qweather.com`

[longpet-weather.ini.example](../deploy/longpet-weather.ini.example) 的 `api_host` 已改为占位符 `https://YOUR_QWEATHER_API_HOST`，注释注明实际值从 QWeather 控制台「设置」获取专属 API Host；全文（含注释）已无 `devapi.qweather.com`。

### 3.2 时间字段解析：不再依赖旧 v7 的 `obsTime/updateTime`

- `parseCurrent` 删除对 `now.obsTime` / 顶层 `updateTime` 的解析；
- `observedAt`：新版响应无观测时间字段 → 保持 invalid；
- `updatedAt`：本机本次成功获取响应的 **UTC 时间**（`QDateTime::currentDateTimeUtc()`），`WeatherService` 兜底同步 UTC；
- `stale` 只依赖最后一次成功获取时间（`isStale`），与 API 字段无关。

### 3.3 humidity 归一化：`0.69 → 69%`

- `WeatherSnapshot.humidity (double)` → `humidityPercent (int)`，语义 0-100；
- 归一化：`≤1.0 → qRound(raw*100)`；`≤100 → qRound(raw)`（兼容百分比形态）；
- 展示：LLM 上下文 `湿度：69%`；测试断言 `context.contains("湿度：69%")`。

### 3.4 HTTP 错误映射修正

| HTTP | 映射 | | 响应 body code | 映射 |
|---|---|---|---|---|
| 400 | ConfigurationError | | 401 | Unauthorized |
| 401 | Unauthorized | | 402/429 | RateLimited |
| 403 | ConfigurationError | | 403/404 | ConfigurationError |
| 404 | **ConfigurationError**（不再 Unauthorized） | | 5xx | ServerError |
| 429 | RateLimited | | 其他 | InvalidResponse |
| 5xx | ServerError | | | |

- 新增 `mapHttpStatus(int)`（原匿名函数提升为可测试静态方法）；
- 新增 `problemDetail(body)`：解析 `application/problem+json` 的 `error.title/detail`（不含超链接 `type`），兼容 `{"code":...,"message":...}`；
- 所有诊断文本进日志；**永不输出 API Key**。

### 3.5 QWeather 来源署名

阶段二曾把署名 `· 天气服务：和风天气` 固定在状态栏天气行（仅在有天气数据时出现），
满足使用条款；因小屏状态栏拥挤，阶段三补充改为：署名 `天气数据：和风天气 (QWeather)`
挂在设置页「关于设备」副标题（见 §4.5），状态栏只留图标 + 温度。

### 3.6 自动测试断言

新增/更新（[tests/V02Test.cpp](../tests/V02Test.cpp)）：

- humidity `"0.69" → humidityPercent == 69`，且兼容 `"65" → 65`；
- 新版 current JSON **无 `obsTime/updateTime`** 仍解析成功：`observedAt` invalid、`updatedAt` 为 UTC 且近似当前时刻；
- **数字形态完整解析**：`"temp":26 / "feelsLike":27.5 / "humidity":0.69 / "icon":100 / "code":200`（全 JSON 数字）
  解析成功，温度 26.0、湿度 69、图标码 "100"——阶段三实测问题的回归护栏；
- `mapHttpStatus(404) != Unauthorized`（400/403/404 → ConfigurationError，401 → Unauthorized，429 → RateLimited，500 → ServerError）；
- `mapResponseCode("403"/"404") → ConfigurationError`；
- problem+json `title: detail` 提取、message 兜底、非 JSON 返回空；
- `.example` 无 `devapi.qweather.com` 且含 `YOUR_QWEATHER_API_HOST`（新用例 `weatherDeploymentExampleComplies`，经 `LONGPET_REPO_DIR` 定位文件）；
- 状态栏紧凑测试：含 `26°`（取 `晴 26°` 末段）且**不含** `天气服务`，高度仍 64px；编码 `100` 时图标可见且资源为 `:/icons/weather-sunny.svg`，编码为空时图标隐藏；
- 设置页来源署名测试：`setDeviceSummary()` 后「关于设备」副标题含 `天气数据：和风天气 (QWeather)`；
- `weatherIconMappingCoversQWeatherCodes`：编码↔路径全表 + 未知/空编码返回空 + 9 张 SVG 在 qrc 中可渲染；
- `WeatherService` 成功刷新后 `SystemStatus.weatherConditionCode` 同步；失败不覆盖；
- 语音上下文测试：含 `湿度：69%`；过期数据提示；UTC 时间本地化展示。

---

## 4. 阶段三：上板实测修正与状态栏天气图标

### 4.1 实测问题（三个独立根因，日志逐一证实）

上板实测：状态栏未显示天气，语音询问天气无有效答复。多轮排查后三个根因全部收敛：

1. **响应字段形态**：`parseCurrent` 对字段用 `QJsonValue::toString()`，而接口返回的是
   **JSON 数字/对象**（实测响应见下），字符串化失败 → `missing now.temp` → `InvalidResponse`；
2. **真实响应结构**：通过板上 curl 拿到真实响应（2026-08-30），新版接口**没有顶层 `code`
   字段、没有 `now` 对象**，结构完全不同：

   ```json
   {"metadata":{"tag":"...","attributions":["https://developer.qweather.com/attribution.html"]},
    "condition":{"text":"阴","code":"104"},
    "temperature":{"value":29.02,"unit":"°C"},
    "feelsLike":{"value":32.05,"unit":"°C"},
    "humidity":0.8, "wind":{...}, "pressure":{...}, "cloudCover":0.44, ...}
   ```

3. **配置 Host 无协议头**：控制台「设置」给的专属 Host 是裸主机名
   （如 `ph6vhhujph.re.qweatherapi.com`），直接当 URL 得到空 scheme，
   `QNetworkAccessManager` 报 `Protocol "" is unknown`，请求根本发不出去——这与
   板上日志 `Weather update failed: code= NetworkError detail= Protocol "" is unknown` 完全吻合。

### 4.2 修正（分层不变、解析内聚）

- `scalarText() / scalarDouble()`：对 JSON 数字、字符串、布尔统一取值，新增/扩展
  `QJsonValue` 字段均走该通道；
- `parseCurrent` 重写为**双形态**：优先新版顶层形态（`temperature.value` 必填、
  `condition.text/code`、`feelsLike.value`、`humidity` 0-1 小数），兼容旧
  `now.temp/text/icon/humidity` 形态；顶层 `code` 只在新版没有、旧版有——
  有 `code` 才校验是否 `"200"`，没有 `code` 直接按成功处理（错误经 HTTP 状态码表达）；
- `currentUrl()`：`api_host` 无 `://` 时自动补 `https://`（用户按控制台裸主机名配置即可）；
- `humidity` 归一化不变（`0.80 → 80`），与阶段二 §3.3 一致；
- 实测响应确认无 `obsTime/updateTime`：`observedAt` 保持 invalid、`updatedAt` 为本机
  UTC 时间戳，与阶段二 §3.2 结论一致；
- `metadata.attributions` 含和风署名链接；来源署名固定展示在设置页「关于设备」，满足使用条款（状态栏紧凑化见 §4.5）。

### 4.3 状态栏图标（用户提供 8 个 SVG）

- `resources/resources.qrc` 注册 9 张天气 SVG（`weather-sunny.svg` 原有 +
  本轮 `cloudy / partly-cloudy / rain / thunderstorm / snow / fog / windy / clear-night`，别名统一 `icons/weather-*.svg`）；
- `weatherIconResource(code)`（[VisualComponents.cpp](../src/widgets/VisualComponents.cpp)）编码映射表，
  未知/空编码返回空串（纯文本降级）；
- 编码沿 `WeatherSnapshot.conditionCode → WeatherService → SystemService::setWeatherSummary(summary, code) →
  SystemStatus.weatherConditionCode → StatusBarWidget` 传递，未新增端口；
- 状态栏在摘要行首插入 20px `SvgIconWidget`，仅在天气有效且编码可映射时可见；
  天气失效（`--`）时图标同步隐藏并清空编码，避免"只有图标没有文字"。

### 4.4 实测根因二：启动竞态——离线跳过之后不再重试

板上日志：

```text
Weather provider: qweather
Weather service starting, provider= qweather refresh_minutes= 30
Weather refresh skipped: network unavailable
```

板子开机时 wlan0/NetworkManager 比应用晚就绪，启动即刷被"离线"跳过；旧实现不监听网络
状态变化，下一次刷新要等 `refresh_minutes=30` 分钟——表现为"状态栏一直没天气、语音
问不到天气"。修正（全部在 `WeatherService` 内，端口与调用方零改动）：

- 监听 `SystemService::statusChanged`，网络由**不可用→可用（上升沿）时立即补刷**，
  wlan0 连上几秒内即出天气；
- 尚无任何数据时：离线跳过、首次拉取失败都以 60 秒短周期重试，直到第一次成功；
- 连续 3 次被判离线仍无数据时执行一次真实请求（探测），防止网络状态后端一直误报
  （如 QNetworkInformation 插件缺失）导致永远拉取不到；
- 拿到第一条数据后立即停止短周期重试，交回 `refresh_minutes` 正常节奏。

`WeatherService.start()` 启动即刷、离线跳过、失败保留旧值等原有行为不变。

### 4.5 状态栏紧凑化：只留图标 + 温度（署名移至设置页「关于设备」）

实机反馈状态栏条目过多（日期/时间/天气/网络/电量/设置）挤在一行，按用户要求改为：

- 天气行只保留 `[图标] 26°` 形式：从 `weatherSummary`（如 "晴 26°"）取最后一个空格后的温度，
  条件文本 "晴" 只体现在图标上，不再出文字
  （[VisualComponents.cpp:177-197](../src/widgets/VisualComponents.cpp#L177)）；
- 来源署名移到设置页「关于设备」副标题：`正式软件版本 · 天气数据：和风天气 (QWeather)`
  （[SettingsPage.cpp:171-175](../src/pages/SettingsPage.cpp#L171)），仍是"合理位置展示来源"，
  满足 QWeather 使用条款；
- `SystemStatus.weatherSummary` 保持完整 "晴 26°"（FamilyLink 载荷与语音注入复用），
  紧凑化只发生在 `StatusBarWidget` 展示层，服务层零改动。

## 5. 文件清单

### 新增（阶段一）

| 文件 | 说明 |
|---|---|
| [WeatherModels.h](../src/model/WeatherModels.h) / [.cpp](../src/model/WeatherModels.cpp) | 快照/配置/错误码/summary/校验 |
| [WeatherPorts.h](../src/services/WeatherPorts.h) | WeatherProviderPort 抽象 |
| [QWeatherProvider.h](../src/platform/QWeatherProvider.h) / [.cpp](../src/platform/QWeatherProvider.cpp) | 和风专属 Host API 实现（HTTP + 解析 + 错误映射 + problem+json） |
| [WeatherProviderFactory.h](../src/platform/WeatherProviderFactory.h) / [.cpp](../src/platform/WeatherProviderFactory.cpp) | 按配置建 Provider；未知厂商占位不崩溃 |
| [WeatherConfigRepository.h](../src/data/WeatherConfigRepository.h) / [.cpp](../src/data/WeatherConfigRepository.cpp) | 独立 ini + 环境变量覆盖 |
| [WeatherService.h](../src/services/WeatherService.h) / [.cpp](../src/services/WeatherService.cpp) | 刷新/缓存/过期/离线/成功失败处理 |
| [longpet-weather.ini.example](../deploy/longpet-weather.ini.example) | 部署模板 |

### 修改（阶段一 + 阶段二）

| 文件 | 说明 |
|---|---|
| [Application.h](../src/app/Application.h) / [.cpp](../src/app/Application.cpp) | 组合根接线、生命周期、`resolveWeatherConfigPath()` |
| [VoiceInteractionService.h/.cpp](../src/services/VoiceInteractionService.cpp) | `setWeatherProvider(λ)` + 天气上下文注入 |
| [VisualComponents.cpp](../src/widgets/VisualComponents.cpp) | 状态栏紧凑天气展示（只留图标 + 温度）；`weatherIconResource()` 编码映射 + 天气图标 |
| [VisualComponents.h](../src/widgets/VisualComponents.h) | `weatherIconResource()` 声明；`SvgIconWidget` 加 `Q_OBJECT`/`resourcePath()` |
| [WeatherService.cpp](../src/services/WeatherService.cpp) | 兜底时间戳改 UTC；快照图标码透传 `setWeatherSummary` |
| [SystemService.h/.cpp](../src/services/SystemService.cpp) | `setWeatherSummary(summary, conditionCode)`，无数据时清图标码 |
| [SettingsPage.h/.cpp](../src/pages/SettingsPage.cpp) | 「关于设备」副标题承载 QWeather 来源署名（阶段三补充） |
| [SystemModels.h](../src/model/SystemModels.h) | `SystemStatus.weatherConditionCode` |
| [QWeatherProvider.cpp](../src/platform/QWeatherProvider.cpp) | 阶段三：`scalarText/scalarDouble` 容忍数字形态 JSON |
| [resources/resources.qrc](../resources/resources.qrc) + [resources/icons/](resources/icons/) | 注册 8 张新天气 SVG（阶段三） |
| [CMakeLists.txt](../CMakeLists.txt) | 新源文件 + `LONGPET_REPO_DIR` 测试宏 |
| [longpet.service](../deploy/longpet.service) | `LONGPET_WEATHER_CONFIG` 环境变量 |
| [配置说明.md](../deploy/配置说明.md) | 天气部署/验收/FAQ 章节（含图标与 `missing now.temp` 排障） |
| [V02Test.cpp](../tests/V02Test.cpp) | §3.6 + §4 全部断言 |

---

## 6. 测试结果

本机 Windows（Qt 6.11.2 msvc2022_64 / MSBuild Release）：

```text
LongPet.V02: Totals: 31 passed, 0 failed, 1 skipped (3223ms)
```

- 跳过项仅 `renderV02Pages`（需设置 `LONGPET_TEST_CAPTURE_DIR`，与天气无关）；
- 天气相关：`weatherConfigurationParsesValidatesAndOverrides`、`weatherDeploymentExampleComplies`、`weatherProviderParsesQWeatherJson`（含数字形态解析）、`weatherServiceRefreshesUpdatesAndDegrades`（含 conditionCode 同步）、`voiceInteractionInjectsWeatherContext`、`statusBarRemains64AndShowsSystemInput`（含图标可见性）、`weatherIconMappingCoversQWeatherCodes` 全部 PASS；
- 既有功能零回归：家联 API、视频通话、语音交互闭环、AI 厂商协议、Provider 工厂、提醒/护理/设置持久化等全部 PASS。

期间过程记录：本机 Smart App Control 曾处于 On，新链接的未签名测试 exe 被系统 WDAC 策略（`{0283ac0f-...}`）拦截（CodeIntegrity 3077/3033：`did not meet the Enterprise signing level requirements`）。用户在设置中关闭智能应用控制后测试得以正常执行；最终以 **31/0/1** 为准。全程未绕过、未关闭任何安全设置去强行运行。

---

## 7. 构建与部署

### Windows（开发）

```bat
cmake -S . -B build -G "Visual Studio 17 2022" -DLONGPET_BUILD_TESTS=ON
cmake --build build --config Release
set PATH=C:\Qt\6.11.2\msvc2022_64\bin;%PATH%
build\Release\LongPetV02Tests.exe -o result.txt,txt
```

### LoongArch64 板子（目标环境）

```bash
./scripts/build-loongarch.sh   # cmake 交叉工具链: cmake/toolchains/loongarch64-buildroot.cmake
```

产出 `/home/justin/build/LongPet-my-loongarch64-release/LongPet`（ELF LoongArch，动态链接），构建零警告（曾消除 5 处 `-Wmissing-field-initializers`：`QWeatherProvider.cpp` 的大括号部分初始化改为 `makeError()` 全字段构造）。

### 板上部署

见 [配置说明.md](../deploy/配置说明.md) §3.5（天气安装/权限/环境变量）、§11（验收）、§15.6（FAQ）。要点：

1. `api_host` 必须是控制台「设置」中的专属 Host；
2. `install -o root -g longpet -m 0640`；
3. 日志开头应有 `Weather service starting, provider= qweather refresh_minutes= 30`；
4. 状态栏出现 `[图标] 26° · 网络正常 · 电量 80%`（设置页「关于设备」含来源署名）；
5. 离线时 `Weather refresh skipped: network unavailable`，不乱清旧数据。

---

## 8. 未实现项与后续扩展

- 逐天预报（"明天会下雨吗"）：需要 `WeatherSnapshot.daily[]`、`fetchForecast()`、`/weather/v1/daily/3d` —— 端口已预留扩展位，新增分支对现有代码影响很小；
- 多城市/语义切城市：当前单坐标来自配置；
- 天气失败在状态栏的告警图标：目前仅日志与 `--`；
- Provider 多实例热切换。

## 9. 相关文档

- 部署：`deploy/配置说明.md`（§2/§3.5/§5/§8/§11/§12/§14/§15.6）
- 上板集成：`docs/LongPet-V0.2-Board-Integration-Report.md`
- 本报告：`docs/LongPet-Weather-Feature-Report.md`
