# LongPet FamilyLink 远程设置与提醒写入报告

- 日期：2026-08-29
- 前置提交：`ab45dd1 feat: add authenticated FamilyLink read API`
- 板端地址：`10.188.219.51`
- API：FamilyLink HTTP v1

## 1. 本轮边界

本轮只扩展 FamilyLink 的设置和提醒写入，不增加云端、公网穿透、WebSocket 或新的板端页面：

1. 远程修改音量、亮度和宠物风格；
2. 远程创建、完整更新和删除提醒；
3. 设置与提醒均使用持久化 revision 防止静默覆盖；
4. 继续要求 Bearer Token；
5. UI、Page 和 Widget 不直接访问网络、SQLite 或硬件；
6. 保留 main 已有 WiFi 和 USB 声卡实现。

## 2. 调用链

设置写入：

```text
Electron Renderer
  -> preload IPC 白名单
  -> FamilyLinkService（客户端校验）
  -> HttpFamilyLinkAdapter
  -> FamilyLinkHttpAdapter（板端 HTTP 收发）
  -> FamilyLinkController（鉴权、JSON、状态码）
  -> FamilyLinkService（板端业务编排与能力检查）
  -> SettingsService（业务校验、信号）
  -> SettingsRepository（事务、持久化 revision）
  -> settingApplyRequested
  -> AudioVolumeAdapter / BacklightAdapter
```

提醒写入：

```text
Electron Renderer
  -> IPC -> FamilyLinkService -> HttpFamilyLinkAdapter
  -> FamilyLinkHttpAdapter -> FamilyLinkController
  -> FamilyLinkService -> ReminderService
  -> ReminderRepository（revision 条件更新/删除）
  -> remindersChanged -> AppController / 板端页面刷新
```

HTTP Adapter 不直接访问数据库；Controller 不绕过 Service。

## 3. 接口

| 方法与路径 | 成功状态 | 说明 |
|---|---:|---|
| `PATCH /api/v1/settings` | 200 | 修改一个或多个可用设置字段 |
| `POST /api/v1/reminders` | 201 | 创建提醒，初始 revision 为 1 |
| `PUT /api/v1/reminders/{id}` | 200 | 使用完整草稿和 expectedRevision 更新 |
| `DELETE /api/v1/reminders/{id}?expectedRevision=n` | 200 | 按 revision 删除 |

状态接口现在报告：

```json
{
  "settingsRead": true,
  "settingsWrite": true,
  "remindersRead": true,
  "remindersWrite": true
}
```

### 3.1 设置写入

```json
{
  "volume": 70,
  "petStyle": "活泼陪伴",
  "expectedRevision": 7
}
```

- 至少提交 `volume`、`brightness`、`petStyle` 中的一项；
- `volume`、`brightness` 为 `0..100` 的整数；
- `petStyle` 为 `温和陪伴` 或 `活泼陪伴`；
- `expectedRevision` 必须等于最近一次 GET 返回的 revision；
- 多字段使用同一 SQLite 事务，只增加一次 revision；
- USB 音量 Adapter 不可用时，提交 volume 返回 503；
- 背光 Adapter 不可用时，提交 brightness 返回 503，且不修改数据库；
- 成功后由 `settingApplyRequested` 同步调用现有硬件 Adapter。

### 3.2 提醒写入

创建和更新字段对齐 `ReminderDraft`：`type`、`title`、`timeOfDay`、`scheduledDate`、`repeatRule`、`enabled`。更新还必须提供 `expectedRevision`。

创建、更新、删除始终经过 `ReminderService`，因此标题、时间、单次提醒未来时间等规则与板端本地操作一致。更新和删除使用 SQL revision 条件；冲突返回当前 revision。

## 4. 错误契约

| HTTP | code | 场景 |
|---:|---|---|
| 401 | `AUTHENTICATION_REQUIRED` | Token 缺失或错误 |
| 404 | `REMINDER_NOT_FOUND` | 提醒不存在 |
| 409 | `REVISION_CONFLICT` | 设置或提醒已被其他操作修改 |
| 413/431 | `REQUEST_TOO_LARGE` | 正文超过 64 KiB 或请求头超过 16 KiB |
| 422 | `VALIDATION_ERROR` | JSON、字段、枚举或范围无效 |
| 503 | `CAPABILITY_UNAVAILABLE` | 对应硬件 Adapter 当前不可用 |

冲突响应示例：

```json
{
  "error": {
    "code": "REVISION_CONFLICT",
    "message": "设置已被其他操作修改，请刷新后重试",
    "details": { "currentRevision": 8 }
  }
}
```

数据库错误、路径和 Token 不会出现在 HTTP 错误中。

## 5. 持久化 revision

设置表仍使用现有 key/value 结构，不做破坏性 schema 迁移。`SettingsRepository` 使用保留键 `settings_revision` 保存 revision：

- 旧数据库没有该键时 revision 从 0 开始；
- 板端本地设置和远程设置都会递增 revision；
- 远程多字段更新在同一事务中提交；
- expectedRevision 不匹配时事务回滚，不写任何字段。

提醒继续使用 `reminders.revision` 列。更新执行 `WHERE id=? AND revision=?`，删除也增加相同条件。

## 6. 自动测试与 Release

Windows LongPet：

- Qt 6.11.2 / MSVC 19.44 / Release 构建成功；
- CTest `LongPet.V02`：1/1 通过；
- `LongPet.exe` SHA-256：`ae0de15cff65072cdabd11110ded39000937d298da526fbec1cc6e5d59ff5350`。

CTest 的真实回环 HTTP 集成测试覆盖：

- Token 401；
- 三个 GET；
- 设置成功写入与 revision 递增；
- 设置旧 revision 返回 409；
- 不可用亮度返回 503 且 revision 不变；
- 提醒创建、更新、旧 revision 冲突和删除；
- 写入后直接验证 Service/Repository 数据。

LoongArch：

- Buildroot GCC 13.3.0 Release 交叉构建成功；
- ELF 为 LoongArch 64-bit、Linux 6.6+；
- SHA-256：`4a7605b00163b31df7f538d363e2bf2c6d0cc3f179a02422bc4f981dcbf449f4`。

family-desktop：

- `npm run check` 通过；
- Node 自动测试 12/12 通过；
- Windows unpacked Release 构建成功；
- `LongPet Family.exe` SHA-256：`b70a953957cac00658d138e6ffc6c44c06c4fda3e4967faa035480dd26866105`。

## 7. 板端部署状态

- 正式二进制：`/home/longpet/LongPet`；
- SHA-256：`4a7605b00163b31df7f538d363e2bf2c6d0cc3f179a02422bc4f981dcbf449f4`；
- systemd：`active/running`，`NRestarts=0`；
- FamilyLink：`0.0.0.0:8787`；
- USB 声卡：`hw:CARD=Device / Speaker · ALSA 音量`；
- 网络：`Wi-Fi · 已联网`；
- `settingsWrite=true`、`remindersWrite=true` 已通过只读状态请求确认。

按用户要求，最终版本部署后 Codex 没有再执行设置或提醒写请求，写入交互由用户手工验证。

## 8. 家属端手工测试

1. 启动 `D:\code\family-desktop\release\win-unpacked\LongPet Family.exe`。
2. 点击“切换连接”。
3. 取消“演示模式”，地址填写 `http://10.188.219.51:8787`。
4. 输入当前板端 FamilyLink Token 并连接。
5. 确认“远程设置”的音量和宠物风格可编辑；本板背光不可用，亮度应保持禁用。
6. 修改音量后保存，预期显示成功且版本号增加；建议先小幅调整，确认音量后再改回需要的值。
7. 新增一条未来时间的测试提醒，确认板端提醒页出现。
8. 编辑标题或时间并保存，确认 revision 更新且板端页面同步。
9. 删除该测试提醒，确认两端均不再显示。

测试期间查看日志：

```sh
journalctl -u longpet.service -f
```

若客户端提示“数据已自动刷新”，表示发生 revision 冲突；确认最新值后再次保存即可。

## 9. 回滚

当前只读 FamilyLink 备份：

```text
/home/longpet/LongPet.backup-familylink-readonly-ab45dd1-20260829
```

回滚命令：

```sh
systemctl stop longpet.service
install -o longpet -g longpet -m 0755 \
  /home/longpet/LongPet.backup-familylink-readonly-ab45dd1-20260829 \
  /home/longpet/LongPet
systemctl start longpet.service
```

回滚后状态接口会恢复 `settingsWrite=false`、`remindersWrite=false`，家属端自动禁用写入按钮。
