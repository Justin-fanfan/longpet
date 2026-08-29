# LongPet 视频通话信令第一步实施报告

> 2026-08-29 更新：本文记录上一阶段“仅信令”基线，完整双向媒体已由 `LongPet-LAN-Bidirectional-Voice-Video-Call-Report.md` 取代；不要再把 `connected + mediaReady=false` 当成可用通话。

日期：2026-08-29  
板端地址：`10.240.178.51`

## 1. 本轮边界

本轮完成视频通话的控制面，不采集或传输摄像头、麦克风数据：

- 首页“提醒”替换为“视频通话”；
- 保留“今日关怀 → 查看提醒 → 提醒页面”；
- LongPet 可以创建呼叫、取消呼叫和挂断；
- 家属端可以发现呼叫、接听、拒绝和挂断；
- 双方通过真实 FamilyLink HTTP API 同步同一个通话状态；
- 使用 `callId + revision` 阻止旧请求操作新通话；
- 通话活跃时暂停 LongPet 的 15 秒页面超时。

`mediaReady` 本轮固定为 `false`。下一小步接入板端 MJPEG 向家属端发送后，才会变为真实媒体就绪状态。

## 2. 分层与信号流

LongPet：

```text
HomePage::videoCallRequested
  -> MainWindow::videoCallRequested
  -> AppController::showVideoCall
  -> VideoCallService::startOutgoingCall
  -> VideoCallService::snapshotChanged
  -> MainWindow::setVideoCallSnapshot
  -> VideoCallPage::setSnapshot
```

远程动作：

```text
FamilyLinkHttpAdapter
  -> FamilyLinkController
  -> FamilyLinkService
  -> VideoCallService::applyRemoteAction
  -> snapshotChanged
  -> AppController / VideoCallPage
```

家属端：

```text
Renderer 通话页面
  -> Preload contextBridge
  -> IpcController
  -> FamilyLinkService
  -> HttpFamilyLinkAdapter
  -> LongPet /api/v1/video-call/*
```

UI 页面不访问数据库、摄像头、声卡或网络。通话状态归 `VideoCallService` 所有；HTTP Adapter 只负责传输。

## 3. 状态机

```text
idle
  -> outgoing_ringing
       -> connected -> ended
       -> rejected
       -> ended（设备取消）
```

每次有效状态转换都递增 `revision`。家属端动作必须同时匹配当前 `callId` 和 `expectedRevision`。

## 4. API

### `GET /api/v1/video-call`

```json
{
  "callId": "7aeaa037-658e-4b39-b5c6-842bb9a6b723",
  "state": "outgoing_ringing",
  "direction": "device_to_family",
  "remoteName": "家属端",
  "startedAt": "2026-08-29T12:00:00Z",
  "updatedAt": "2026-08-29T12:00:00Z",
  "revision": 1,
  "mediaReady": false
}
```

### `POST /api/v1/video-call/actions`

```json
{
  "callId": "7aeaa037-658e-4b39-b5c6-842bb9a6b723",
  "action": "accept",
  "expectedRevision": 1
}
```

`action` 支持 `accept`、`reject`、`hangup`。版本冲突、通话标识不匹配或当前状态不允许该动作时返回 HTTP 409。

所有接口继续使用 FamilyLink Bearer Token；没有新增 SSH、数据库或 root 权限通道。

## 5. 主要代码变更

- `src/model/VideoCallModels.h`：状态、动作、快照和结果模型；
- `src/services/VideoCallService.*`：状态机与乐观版本；
- `src/pages/VideoCallPage.*`：只呈现状态并发出挂断/返回意图；
- `src/pages/HomePage.*`：首页入口替换；
- `src/app/AppController.*`：UI 与 Service 编排；
- `src/app/Application.*`：依赖装配；
- `src/app/FamilyLinkController.*`：JSON 校验和 API 路由；
- `src/services/FamilyLinkService.*`：向 HTTP 层暴露视频通话用例；
- `src/mainwindow.*`：页面注册与信号转发；
- `tests/V02Test.cpp`：状态机、API、导航和页面回归测试。

## 6. 构建与板端验收

- Windows Qt 6.11.2 / MSVC Release：成功；
- CTest `LongPet.V02`：1/1 通过；
- LoongArch Buildroot GCC 13.3.0 Release：成功；
- 家属端静态检查：通过；
- 家属端 Node 测试：15/15 通过；
- 家属端 unpacked Release：成功；
- 板端 GStreamer、FFmpeg、V4L2、WebRTC/ALSA/VP8/Opus 插件：已验收；
- 板端新二进制：`/home/longpet/LongPet`；
- 板端新二进制 SHA-256：`e87488d6fd71a8bbf1221bcbe018c8f87103021035afb518eccc40481b8775c8`；
- 板端旧版备份：`/home/longpet/LongPet.before-video-signaling-4a7605b0`；
- `longpet.service`：`active`；
- 板端 `GET /api/v1/video-call`：成功返回初始 `idle`。

上传用临时文件已经删除，旧版备份保留。

## 7. 人工验收步骤

1. 打开 LongPet 控制页，确认首页第三个按钮为“视频通话”；
2. 进入“今日关怀”，确认“查看提醒”仍能进入提醒页面；
3. 家属端配置 `http://10.240.178.51:8787` 和原配对令牌；
4. LongPet 点击“视频通话”；
5. 家属端应在约一秒内自动进入视频通话页；
6. 家属端点击“接听”，LongPet 应显示“视频通话中”；
7. 任意一端挂断，双方应显示通话结束；
8. 再发起一次并选择“拒绝”，LongPet 应显示家属端暂时无法接听。

## 8. 下一小步

增加媒体 WebSocket Adapter 和 V4L2 MJPEG Adapter，实现 `/dev/video0` 原生 MJPEG 约 15 fps 单向发送到家属端。该步骤不修改本轮状态 API。
