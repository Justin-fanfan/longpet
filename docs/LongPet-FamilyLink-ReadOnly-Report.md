# LongPet FamilyLink 只读真实连接报告

- 日期：2026-08-29
- 代码基线：`main@884a4a9`
- 板端：Loongson LS2K300 / Buildroot / Qt 6.8.1
- 板端地址：`10.188.219.51`
- API：FamilyLink HTTP v1，只读阶段

## 1. 本轮边界

本轮把家属端真实连接能力手工迁移到最新 main：

1. 读取设备、系统和今日关怀状态；
2. 读取当前用户设置及硬件能力；
3. 读取板端 SQLite 中的提醒；
4. 明确拒绝所有远程写请求；
5. 保留 main 已有的 WiFi 配置和 USB 声卡适配；
6. 不让页面、Widget 直接访问数据库、硬件或网络。

远程设置写入和提醒增删改不在本轮范围内。

## 2. 调用链

```text
QTcpServer / QTcpSocket
  -> FamilyLinkHttpAdapter（HTTP 收发、报文大小限制）
  -> FamilyLinkController（路由、鉴权、JSON DTO）
  -> FamilyLinkService（只读业务编排）
  -> SystemService / SettingsService / ReminderService / CareService
  -> Repository / 已有硬件 Adapter
```

`Application` 仍是唯一组合根，负责创建、启动、停止和销毁 FamilyLink 对象。`MainWindow`、Page 和 Widget 没有新增网络依赖。WiFi 仍经由 `AppController -> NetworkService -> NetworkManagerAdapter` 工作。

## 3. 接口

| 方法与路径 | 数据来源 | 当前权限 |
|---|---|---|
| `GET /api/v1/status` | `SystemService`、`CareService` | 可读 |
| `GET /api/v1/settings` | `SettingsService`、`SystemService` 能力摘要 | 可读 |
| `GET /api/v1/reminders` | `ReminderService` | 可读 |
| 上述路径的非 GET 请求 | 无写入调用 | HTTP 405 |

状态响应包含能力声明：

```json
{
  "settingsRead": true,
  "settingsWrite": false,
  "remindersRead": true,
  "remindersWrite": false
}
```

所有时间统一输出为 UTC ISO 8601，例如 `2026-08-29T00:56:46Z`。家属端负责按电脑本地时区显示。

## 4. 网络与安全边界

- 默认只监听 `127.0.0.1:8787`；
- 只有显式配置 `LONGPET_FAMILY_LINK_ADDRESS` 才会改变监听地址；
- 非回环监听必须同时配置 `LONGPET_FAMILY_LINK_TOKEN`，否则拒绝启动 HTTP 服务；
- Token 通过 `Authorization: Bearer <token>` 传递；
- 请求头上限 16 KiB；
- 每个响应使用 `Connection: close` 和 `Cache-Control: no-store`；
- 未授权请求返回 HTTP 401；
- 禁止把 8787 端口直接映射到公网。

板端密钥保存在 root-only 的 systemd drop-in 中，不写入 Git、报告或日志。

## 5. 修改文件

新增：

- `src/services/FamilyLinkService.h/.cpp`
- `src/app/FamilyLinkController.h/.cpp`
- `src/platform/FamilyLinkHttpAdapter.h/.cpp`
- `deploy/longpet-familylink.conf.example`
- `docs/LongPet-FamilyLink-ReadOnly-Report.md`

修改：

- `CMakeLists.txt`
- `src/app/Application.h/.cpp`
- `tests/V02Test.cpp`
- `README.md`

旧 Codex worktree 中已有的 `scripts/build-loongarch.sh` 修改没有迁移；最新 main 的 WiFi 和 USB 声卡代码没有被覆盖。

## 6. 构建与测试

Windows：

- Qt `6.11.2` / MSVC `19.44` / Release；
- `LongPet.exe` 构建成功；
- CTest：`LongPet.V02` 1/1 通过，耗时 2.99 秒；
- Windows 二进制 SHA-256：`535649a0b61b30d8512a50bc97fe5bbcbe82102d20d72b50bedf9bceddd333fe`。

FamilyLink 集成测试实际监听随机回环端口，验证：

- 无 Token 请求返回 401；
- 带 Token 的三个 GET 返回真实 Service 数据；
- 时间字段使用 UTC `Z`；
- PATCH 设置返回 405 `READ_ONLY_API`。

LoongArch：

- Buildroot SDK / GCC `13.3.0`；
- Release 交叉构建成功；
- ELF：LoongArch 64-bit，Linux 6.6+；
- 继续只依赖现有 `Qt6::Network`，未引入 Qt HttpServer；
- 板端二进制 SHA-256：`36e2e464e5e40dfb74405ce916762952262f1b13d6fd34eb0b5b173554ec1dca`。

家属端：

- Node 自动化测试 11/11 通过；
- Windows 打包程序使用真实板端地址和 Token 完成截图冒烟测试，退出码为 0。

## 7. 板端部署与实测

- systemd 单元：`/etc/systemd/system/longpet.service`；
- FamilyLink drop-in：`/etc/systemd/system/longpet.service.d/familylink.conf`，权限 `0600`；
- 正式二进制：`/home/longpet/LongPet`；
- 运行用户：`longpet:longpet`；
- 回滚备份：`/home/longpet/LongPet.backup-main-884a4a9-before-familylink-20260829`；
- 当前服务：`active/running`，`NRestarts=0`；
- 监听：`0.0.0.0:8787`，由 LongPet 进程持有；
- Windows 到 `10.188.219.51:8787` 的带 Token 请求成功。

真实返回值包括：

- 设备 ID：`longpet-ls-gd`；
- 网络：`Wi-Fi · 已联网`；
- 音频：`hw:CARD=Device / Speaker · ALSA 音量`；
- 饮水：`1/8`；
- 设置：音量 `71`、亮度 `100`、风格 `温和陪伴`；
- 提醒：4 条；
- 无 Token：HTTP 401，错误码 `AUTHENTICATION_REQUIRED`；
- PATCH 设置：HTTP 405，错误码 `READ_ONLY_API`。

背光仍报告 `写入背光失败：Permission denied`，这是已知板端能力现状，本轮不处理。

## 8. 安装配置

将 `deploy/longpet-familylink.conf.example` 复制为：

```text
/etc/systemd/system/longpet.service.d/familylink.conf
```

把占位 Token 替换为强随机值，设置权限并重启：

```sh
chown root:root /etc/systemd/system/longpet.service.d/familylink.conf
chmod 0600 /etc/systemd/system/longpet.service.d/familylink.conf
systemctl daemon-reload
systemctl restart longpet.service
```

## 9. 回滚

```sh
systemctl stop longpet.service
install -o longpet -g longpet -m 0755 \
  /home/longpet/LongPet.backup-main-884a4a9-before-familylink-20260829 \
  /home/longpet/LongPet
systemctl start longpet.service
```

如需同时关闭 FamilyLink，再移除对应 drop-in 后执行 `systemctl daemon-reload`。删除配置属于运维操作，应在明确需要回滚接口时执行。

## 10. 下一步建议

下一轮只实现设置写入：先为设置建立持久化 revision，再在 `PATCH /api/v1/settings` 中完成校验、冲突检测、Service 持久化和硬件应用结果回传。提醒写入继续留到再下一轮。
