# LongPet V0.2 代码审查报告

- 审查日期：2026-08-14（由代码审查 Agent 执行）
- 工程目录：`D:\code_qt\longpet`
- 审查范围：`src/` 全部 46 个源文件、`tests/V02Test.cpp`、`CMakeLists.txt`、`resources/`、`cmake/toolchains/`、`.gitignore` 与 `README.md`
- 审查方式：**静态代码审查**（未重新编译或上机运行；构建产物与测试结果文件已存在于工作区，仅作参考）
- 结论速览：**架构优秀、工程规范、测试到位，可作为后续版本基线；发现 1 个高 DPI 潜在渲染缺陷、若干中等/低级别的可维护性与文档一致性事项，均不影响 1024×600 目标机 dpr=1 的当前运行。**

---

## 1. 总体评价

LongPet V0.2 是一个面向 1024×600 触控终端的 Qt 6 Widgets 应用，已从 V0.1 的「UI 骨架」推进到「本地业务真实闭环」。整体工程质量明显高于同类原型项目，主要体现在：

- 分层清晰、依赖方向单一（`Page → MainWindow → AppController → Service → Repository → SQLite`）；
- `Application` 作为组合根统一管理对象生命周期，`main.cpp` 职责极简；
- 数据访问全部使用参数化 SQL，无注入风险；
- 提醒更新采用 `revision` 乐观并发控制；
- SQLite 版本化 + 事务迁移；
- 未接入能力明确显示「待接入」，不伪造数据；
- 自动化测试覆盖资源、Schema、CRUD、调度防重、导航、状态栏、页面渲染。

以下是按严重级别整理的问题清单与改进建议。

---

## 2. 问题清单（按严重级别）

### 🔴 高（潜在正确性缺陷）

#### H-1 `PetFaceWidget` 静态缓存渲染未按 devicePixelRatio 缩放

- 文件：`src/widgets/PetFaceWidget.cpp`
- 位置：`renderStaticCache()`（L306–319）、`applyLogicalTransform()`（L334–340）、`paintEvent()`（L380–390）

**问题**：`renderStaticCache()` 创建了 `width*dpr × height*dpr` 像素的 `QImage` 并 `setDevicePixelRatio(dpr)`，但随后的 `QPainter` **没有做 `painter.scale(dpr, dpr)`**，而是直接用 `applyLogicalTransform()`（基于逻辑尺寸 `width()/height()`）在**物理像素坐标**里绘图。于是脸部只画在图像左上角 `width×height` 像素区域；`paintEvent()` 里 `drawImage()` 再按图像逻辑尺寸（= 像素/dpr）1:1 映射回控件，最终在 `dpr > 1` 的屏上脸部会被缩小到左上角 1/dpr 区域。

**影响**：当前目标机（1024×600、dpr=1）不受影响，测试也仅在 offscreen dpr=1 下抓图。但一旦部署到 HiDPI 面板（或 Qt 高缩放环境），宠物脸会错位缩小，属于**潜伏缺陷**。

**建议**：在 `renderStaticCache()` 中、`applyLogicalTransform()` 之前补一行 `painter.scale(dpr, dpr);`（并在 cache key 已含 dpr 的基础上保持现状即可）。补一条 dpr=2 的渲染回归用例（用 `QScreen` 或离屏 `QPixmap` 模拟 dpr 较复杂，可退而求其次：直接对 `renderStaticCache` 私有逻辑做单元验证，或至少人工在 dpr=2 环境确认一次）。

---

### 🟠 中（功能完整性 / 一致性）

#### M-1 「已错过」状态只计算、不落库，与文档声明不符

- 文件：`src/services/ReminderService.cpp` L39–45；`src/data/ReminderRepository.cpp` `statusToString()` L41–50
- 相关文档：`docs/LongPet-V0.2-Work-Report.md` §4.2 表格声称 `reminder_events` 记录「投递、完成和**错过**」

**问题**：`ReminderOccurrenceStatus::Missed` 的展示完全在 `ReminderService::reminders()` 里按 `occurredWithoutDelivery` 即时推算，**没有任何代码路径写入 `status='missed'` 的 `reminder_events` 记录**。`statusToString()` 虽已支持 `"missed"`，但实际是无用分支。

**影响**：「已错过」仅存在于 UI 展示，不持久化，无法形成历史报表/统计；文档中「错过记录」表述与实现不符。

**建议**：二选一——(a) 在调度补偿或 `reminders()` 识别到错过时补写一条 missed 事件；(b) 若设计上就是「推导不落库」，请修正工作报告的措辞，并删除/注释 `statusToString` 的 missed 分支以免误导。

#### M-2 编辑页永远写 `enabled=true`，停用态实为死代码

- 文件：`src/pages/ReminderEditPage.cpp` `currentDraft()` L160–174（`draft.enabled = true;` L172）
- 相关：`ReminderOccurrenceStatus::Disabled` 仅能在 `reminders()` 读到 `enabled=false` 时出现（`ReminderService.cpp` L34–37）

**问题**：编辑页无「停用/启用」开关，且保存时强制 `enabled=true`。数据模型、Repository、Service 与 QSS 都完整支持 `Disabled` 状态，但 UI 层没有任何入口能产生它，属于**未接通的死路径**。

**影响**：功能无损失，但测试/维护者可能误以为「停用」已实现；`Disabled` 相关分支无法被集成测试覆盖。

**建议**：若停用属于后续版本，保持现状但明确标注；否则在编辑页加「启用」开关并补对应测试。

#### M-3 饮水目标「8 杯」在 Service 与模型两处硬编码

- 文件：`src/services/CareService.cpp` `recordWater()` L52（`current >= 8`）；`src/model/ReminderModels.h` L63（`waterGoal = 8`）

**问题**：目标值在两处独立硬编码。当前一致，但一旦「目标可配置」（工作报告 §11 已列为待定产品决策），`recordWater()` 的上限判断不会跟随 `waterGoal`，会产生「页面显示目标 10 杯、Service 却在 8 杯就拒绝」的漂移。

**建议**：让 `recordWater()` 从统一来源（如 `todaySummary().waterGoal` 或一个配置常量）读取目标，至少收敛为单一常量/常量函数。

---

### 🟡 低（可维护性 / 健壮性 / 规范）

#### L-1 `SettingsService::settings()` 复用同一 `bool valid` 解析两处

- 文件：`src/services/SettingsService.cpp` L14–20

`volume` 与 `brightness` 共用 `valid`，功能正确但可读性差，且第二次 `toInt(&valid)` 会覆盖第一次的解析结果（虽不影响逻辑）。建议各自独立 bool。

#### L-2 宠物风格枚举字符串在两处重复

- `src/services/SettingsService.cpp` L44–47 定义白名单；`src/pages/SettingsPage.cpp` L135–136 硬编码同样的两串做切换。

两处漂移会导致「按钮切到某值、Service 拒收」且用户看到错误 toast。建议把白名单提升为模型级常量/`QStringList`，页面切换逻辑从该常量派生。

#### L-3 状态栏设置按钮尺寸 QSS(62) 与代码(64) 不一致

- `src/widgets/VisualComponents.cpp` L85–86 `setFixedSize(StatusBarHeight, StatusBarHeight)`（=64）；`resources/styles/app.qss` L137–143 `statusAction` `min/max-width/height: 62px`。

当前测试断言 64×64 通过，但 QSS 与代码的 `min/max` 互相拉扯，语义含糊。建议统一为 64（或改为不设 QSS 尺寸、完全由代码控制）。

#### L-4 控制页 15 秒超时同样作用于「编辑提醒」页，且日历弹出窗不参与过滤

- `src/app/AppController.cpp` L25–27 / L146–153；`src/mainwindow.cpp` `eventFilter()` L156–174

编辑提醒是打字场景，15 秒可能过短（工作报告 §11 决策 11 也已自认）。此外 `QDateEdit` 的日历弹出框是独立顶层窗口，不在 `eventFilter` 的 `current->isAncestorOf(...)` 判断内，用户操作日历期间超时仍会倒计时跳回陪伴页。建议：编辑页单独放宽超时（如 60s），并把弹出框交互纳入活动判定或暂停计时。

#### L-5 时区/日期边界依赖「统一 UTC 字符串 + 字典序比较」

- `src/data/ReminderRepository.cpp` L186–225、`CareEventRepository.cpp` L29–44

当前所有时间都写成 `Qt::ISODate` 的 UTC（带 `Z`），字典序比较等价于时间比较，且「本地日 → UTC 边界」换算正确，因此**功能正确**。但这一正确性高度依赖「所有写入都必须走 UTC + 同一格式」。建议抽一个 `dateRangeUtc(date)` 工具函数统一生成上下界并加注释，避免未来有人写入本地时间串破坏比较。

#### L-6 版本号展示多出 `.0`

- `src/pages/SettingsPage.cpp` L164–165 显示 `LongPet V%1` 且 `softwareVersion=0.2.0`，结果为 `LongPet V0.2.0`；README/标题为 `V0.2`。属外观不一致，建议统一显示口径。

#### L-7 无结构化日志，仅 `qWarning/qCritical`

- `src/main.cpp` L19、L25；各 Service/Controller 错误仅靠 toast + 返回码。

对嵌入式长稳运行（工作报告 §10.7 要求 24–72h 验证）而言，缺少带轮转的日志会显著降低排障效率。建议引入 Qt `QLoggingCategory` + 文件 sink（qInstallMessageHandler），后续版本落地。

#### L-8 保留页与 QSS 存在死代码/演示文案

- `src/pages/ConversationPage.cpp`（「UI 示例」「不包含语音播放」）、`EmergencyPage.cpp`、`SleepPage.cpp`（硬编码 `23:48`、`8月11日`）；`VisualTokens.h` 中 `BackButtonSize/PrimaryButtonHeight/EmergencyButtonHeight/PageMargin` 未见使用；`app.qss` 中 `devTab/demoLink/faceTile/demoBadge/reviewBadge/QCheckBox/cardVariant` 等规则未引用。

这些是 README 明确「保留供后续使用」的资产，**属有意为之，非缺陷**。建议为这些保留项加一处清单注释，避免后续清理时被误删或误判为 bug。

#### L-9 `.gitignore` 与工作报告自述不一致

- `.gitignore` L18–19：`docs/*` 生效，`!docs/LongPet-V0.2-Work-Report.md` 被注释掉。实测 `git check-ignore` 确认该报告**未被纳入版本库**，而 `docs/LongPet_V0.1_UI套用说明.md` 却在库中。工作报告 §14 声称「允许 V0.2 报告进入仓库」与现状矛盾。建议决定并统一：要么取消注释纳入该报告，要么修正文档表述。

---

## 3. 分层质量评估

| 层 | 评估 | 备注 |
|---|---|---|
| `src/app`（组合根/控制器） | ⭐⭐⭐⭐⭐ | 职责清晰、生命周期有序、`shutdown()` 幂等；信号全用语义事件 |
| `src/data`（SQLite/Repository） | ⭐⭐⭐⭐⭐ | 参数化查询、事务迁移、乐观锁、外键/忙等待 PRAGMA 齐全 |
| `src/services` | ⭐⭐⭐⭐ | 单定时器调度、防重投递、防抖写库；`Missed` 未落库（M-1）为唯一明显缺口 |
| `src/pages` | ⭐⭐⭐⭐ | 页面零 SQL/硬件依赖，语义信号完整；编辑页 `enabled` 恒真（M-2）待接通 |
| `src/widgets` | ⭐⭐⭐⭐ | 组件复用到位、缓存/区域重绘优化用心；高 DPI 缓存缺陷（H-1）需修 |
| `resources` | ⭐⭐⭐⭐ | QSS 令牌化、SVG 齐全可解析；存在未使用规则（L-8） |

---

## 4. 测试覆盖评估

`tests/V02Test.cpp` 质量较高，覆盖：资源内嵌与 SVG 有效性、Schema/版本、Reminder CRUD + revision 冲突、当日防重投递、Care/Settings 持久化、页面语义信号、AppController 导航、15s 超时、状态栏 64px 与跨页广播、6 页 1024×600 渲染。

**缺口建议**：

1. **无 dpr>1 的渲染测试**（对应 H-1）——当前渲染用例固定 dpr=1，掩盖了高 DPI 缺陷；
2. **无「编辑后 enabled=false / 停用态」测试**（对应 M-2）——死路径无法回归；
3. **无时区边界测试**——`sumForDate/hasEventForDate` 的 UTC 边界依赖当前时区，建议用非 UTC 环境或注入时钟验证一次跨天统计；
4. `renderV02Pages` 依赖 `LONGPET_TEST_CAPTURE_DIR`，未设时 `QSKIP`，CI 默认不渲染，建议 CI 显式设置该变量以纳入验收。

---

## 5. 工程与构建

- `CMakeLists.txt`：`LongPetCore` OBJECT 库让主程序/测试共享一次编译，是良好的构建复用实践；警告开关（`/W4 /permissive-` / `-Wall -Wextra -Wpedantic`）到位；`LONGPET_BUILD_TESTS` 开关清晰。
- 交叉编译工具链 `cmake/toolchains/loongarch64-buildroot.cmake`：LoongArch64 目标、sysroot、`Qt6_DIR`、`QT_HOST_PATH` 与 `CMAKE_FIND_ROOT_PATH_MODE_*` 配置合理，注释清楚。
- 仓库卫生：工作区内存在多个历史构建目录（`build-win-*`），已被 `/build*/` 忽略、未入库，不影响交付；建议本地清理以减小误读风险。

---

## 6. 风险与建议优先级

| 优先级 | 事项 | 关联 |
|---|---|---|
| P0（上机前确认） | SQLite driver 可发现、目录可写、`LONGPET_DATABASE_PATH` 明确、断电重开、备份后升级 | 报告 §10.1 |
| P0 | 网络/电量/天气 Adapter、音量/背光 Adapter 真实接入，采集异步化 | 报告 §10.2/10.3 |
| P1（修缺陷） | 修复 H-1 高 DPI 缓存缩放（`painter.scale(dpr,dpr)`） | 本文 H-1 |
| P1 | 决定 M-1「错过是否落库」并统一文档 | 本文 M-1 |
| P2（收敛） | M-3 饮水目标单一来源、L-2 宠物风格白名单收敛、L-9 统一 .gitignore | 本文 |
| P2 | 补充 dpr>1、停用态、时区边界测试 | §4 |

---

## 7. 结论

LongPet V0.2 的分层架构、数据持久化、乐观并发、防重投递调度和测试覆盖都达到了可交付的工程水准，报告中「本地业务真实闭环」的自我评价基本属实。**未发现会导致当前目标机（1024×600、dpr=1）崩溃或数据损坏的缺陷**。

需重点跟进的只有两件事：

1. **H-1 高 DPI 静态缓存缩放缺陷**——现在修成本最低，等上了 HiDPI 面板再查会很难定位；
2. **M-1「已错过」不落库与文档表述的矛盾**——决定是实现还是改文档，避免下一版本基于错误前提继续开发。

其余为可维护性与文档一致性级别的改进，可按 P1/P2 优先级随迭代消化。
