#include "VideoCallPage.h"

#include "widgets/PetFaceWidget.h"

#include <QGridLayout>
#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <QStyle>
#include <QVBoxLayout>

class VideoFrameWidget final : public QWidget {
public:
    using QWidget::QWidget;

    void setFrame(const QImage& frame)
    {
        m_frame = frame;
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this);
        painter.fillRect(rect(), Qt::black);
        if (m_frame.isNull())
            return;
        const qreal sourceAspect = qreal(m_frame.width()) / m_frame.height();
        const qreal targetAspect = qreal(width()) / qMax(1, height());
        QRectF source(0, 0, m_frame.width(), m_frame.height());
        if (targetAspect > sourceAspect) {
            const qreal wantedHeight = m_frame.width() / targetAspect;
            source.setTop((m_frame.height() - wantedHeight) / 2.0);
            source.setHeight(wantedHeight);
        } else {
            const qreal wantedWidth = m_frame.height() * targetAspect;
            source.setLeft((m_frame.width() - wantedWidth) / 2.0);
            source.setWidth(wantedWidth);
        }
        painter.drawImage(QRectF(rect()), m_frame, source);
    }

private:
    QImage m_frame;
};

namespace {
QString titleFor(const VideoCallSnapshot& snapshot)
{
    const QString kind = snapshot.mode == VideoCallMode::Video
        ? QStringLiteral("视频通话") : QStringLiteral("语音通话");
    switch (snapshot.state) {
    case VideoCallState::Idle: return kind;
    case VideoCallState::OutgoingRinging: return QStringLiteral("正在呼叫家属端");
    case VideoCallState::NotifyingDevice: return QStringLiteral("家属端正在呼叫");
    case VideoCallState::ConnectingMedia: return QStringLiteral("正在建立媒体通道");
    case VideoCallState::Connected: return QStringLiteral("%1中").arg(kind);
    case VideoCallState::Rejected: return QStringLiteral("家属端暂时无法接听");
    case VideoCallState::Ended: return QStringLiteral("通话已结束");
    case VideoCallState::Failed: return QStringLiteral("通话连接失败");
    }
    return kind;
}

QString descriptionFor(const VideoCallSnapshot& snapshot)
{
    switch (snapshot.state) {
    case VideoCallState::Idle: return QStringLiteral("准备呼叫家属端");
    case VideoCallState::OutgoingRinging: return QStringLiteral("请稍候，正在等待家属端接听……");
    case VideoCallState::NotifyingDevice: return QStringLiteral("正在播放来电提示，随后将自动接通");
    case VideoCallState::ConnectingMedia: return QStringLiteral("正在打开麦克风和扬声器……");
    case VideoCallState::Connected: return QStringLiteral("已连接到家属端");
    case VideoCallState::Rejected: return QStringLiteral("您可以稍后再次呼叫");
    case VideoCallState::Ended: return QStringLiteral("感谢您的陪伴");
    case VideoCallState::Failed: return snapshot.errorMessage.isEmpty()
        ? QStringLiteral("媒体设备或网络连接不可用") : snapshot.errorMessage;
    }
    return {};
}
}

VideoCallPage::VideoCallPage(QWidget* parent)
    : QWidget(parent)
{
    setProperty("page", true);
    setObjectName(QStringLiteral("videoCallPage"));
    setAttribute(Qt::WA_StyledBackground, true);

    auto* root = new QGridLayout(this);
    root->setContentsMargins(0, 0, 0, 0);

    m_remoteVideo = new VideoFrameWidget(this);
    m_remoteVideo->setObjectName(QStringLiteral("remoteVideoFrame"));
    root->addWidget(m_remoteVideo, 0, 0);

    m_audioStage = new QWidget(this);
    m_audioStage->setObjectName(QStringLiteral("voiceCallStage"));
    m_audioStage->setStyleSheet(QStringLiteral(
        "#voiceCallStage { background: #121210; }"));
    auto* audioLayout = new QVBoxLayout(m_audioStage);
    audioLayout->setContentsMargins(0, 0, 0, 0);
    m_speakingFace = new PetFaceWidget(PetExpression::Speaking, m_audioStage);
    m_speakingFace->setBackgroundColor(QColor(QStringLiteral("#121210")));
    m_speakingFace->setAnimationEnabled(true);
    audioLayout->addWidget(m_speakingFace);
    root->addWidget(m_audioStage, 0, 0);

    auto* statusOverlay = new QWidget(this);
    statusOverlay->setObjectName(QStringLiteral("callStatusOverlay"));
    statusOverlay->setStyleSheet(QStringLiteral(
        "#callStatusOverlay { background: rgba(11,11,10,190); border-radius: 18px; }"
        "#callStatusOverlay QLabel { color: white; background: transparent; }"));
    auto* statusLayout = new QVBoxLayout(statusOverlay);
    statusLayout->setContentsMargins(20, 12, 20, 12);
    statusLayout->setSpacing(3);
    m_title = new QLabel(QStringLiteral("视频通话"), statusOverlay);
    m_title->setObjectName(QStringLiteral("videoCallTitle"));
    m_title->setStyleSheet(QStringLiteral("font-size: 28px; font-weight: 800;"));
    m_remoteName = new QLabel(QStringLiteral("家属端"), statusOverlay);
    m_remoteName->setObjectName(QStringLiteral("videoCallRemoteName"));
    m_description = new QLabel(QStringLiteral("准备呼叫家属端"), statusOverlay);
    m_description->setObjectName(QStringLiteral("videoCallDescription"));
    m_callId = new QLabel(statusOverlay);
    m_callId->setObjectName(QStringLiteral("videoCallId"));
    m_duration = new QLabel(QStringLiteral("00:00"), statusOverlay);
    m_duration->setObjectName(QStringLiteral("videoCallDuration"));
    m_duration->setStyleSheet(QStringLiteral("font-size: 22px; font-weight: 700;"));
    statusLayout->addWidget(m_title);
    statusLayout->addWidget(m_remoteName);
    statusLayout->addWidget(m_description);
    statusLayout->addWidget(m_duration);
    statusLayout->addWidget(m_callId);
    root->addWidget(statusOverlay, 0, 0, Qt::AlignTop | Qt::AlignLeft);

    m_controlOverlay = new QWidget(this);
    m_controlOverlay->setObjectName(QStringLiteral("callControlOverlay"));
    auto* actions = new QVBoxLayout(m_controlOverlay);
    actions->setContentsMargins(0, 0, 0, 30);
    m_backButton = new QPushButton(QStringLiteral("返回首页"), m_controlOverlay);
    m_backButton->setObjectName(QStringLiteral("videoCallBackButton"));
    m_backButton->setProperty("role", "secondary");
    m_hangUpButton = new QPushButton(QStringLiteral("挂断通话"), m_controlOverlay);
    m_hangUpButton->setObjectName(QStringLiteral("videoCallHangUpButton"));
    m_hangUpButton->setProperty("role", "dangerCompact");
    m_backButton->setFixedSize(260, 86);
    m_hangUpButton->setFixedSize(300, 96);
    actions->addWidget(m_backButton);
    actions->addWidget(m_hangUpButton);
    root->addWidget(m_controlOverlay, 0, 0, Qt::AlignBottom | Qt::AlignHCenter);

    connect(m_backButton, &QPushButton::clicked,
            this, &VideoCallPage::backRequested);
    connect(m_hangUpButton, &QPushButton::clicked,
            this, &VideoCallPage::hangUpRequested);
    m_controlsTimer.setSingleShot(true);
    m_controlsTimer.setInterval(4'000);
    connect(&m_controlsTimer, &QTimer::timeout,
            m_controlOverlay, &QWidget::hide);
    m_durationTimer.setInterval(1'000);
    connect(&m_durationTimer, &QTimer::timeout,
            this, &VideoCallPage::updateDuration);
    m_durationTimer.start();
    setSnapshot({});
}

void VideoCallPage::setSnapshot(const VideoCallSnapshot& snapshot)
{
    const VideoCallState previousState = m_snapshot.state;
    const QString previousCallId = m_snapshot.callId;
    m_snapshot = snapshot;
    if (snapshot.callId != previousCallId)
        m_remoteVideo->setFrame({});
    m_title->setText(titleFor(snapshot));
    m_description->setText(descriptionFor(snapshot));
    m_remoteName->setText(snapshot.remoteName.isEmpty()
        ? QStringLiteral("家属端") : snapshot.remoteName);
    m_callId->setText(snapshot.callId.isEmpty()
        ? QString() : QStringLiteral("通话编号 %1").arg(snapshot.callId.left(8)));

    const bool active = snapshot.isActive();
    m_backButton->setVisible(!active);
    m_hangUpButton->setVisible(active);
    m_hangUpButton->setText(snapshot.state == VideoCallState::Connected
        ? QStringLiteral("挂断通话") : QStringLiteral("取消呼叫"));
    const bool video = snapshot.mode == VideoCallMode::Video;
    m_remoteVideo->setVisible(video);
    m_audioStage->setVisible(!video);
    m_speakingFace->setAnimationEnabled(!video && active);
    updateDuration();

    if (!active) {
        m_controlsTimer.stop();
        m_controlOverlay->show();
    } else if (snapshot.state == VideoCallState::Connected) {
        if (previousState != VideoCallState::Connected)
            m_controlOverlay->hide();
    } else {
        m_controlOverlay->show();
    }
}

void VideoCallPage::setRemoteVideoFrame(const QImage& frame)
{
    m_remoteVideo->setFrame(frame);
}

void VideoCallPage::showControlsTemporarily()
{
    if (!m_snapshot.isActive())
        return;
    m_controlOverlay->show();
    m_controlOverlay->raise();
    m_controlsTimer.start();
}

void VideoCallPage::updateDuration()
{
    if (!m_snapshot.connectedAt.isValid()
        || m_snapshot.state != VideoCallState::Connected) {
        m_duration->setText(QStringLiteral("00:00"));
        return;
    }
    const qint64 seconds = qMax<qint64>(0,
        m_snapshot.connectedAt.secsTo(QDateTime::currentDateTimeUtc()));
    m_duration->setText(QStringLiteral("%1:%2")
        .arg(seconds / 60, 2, 10, QLatin1Char('0'))
        .arg(seconds % 60, 2, 10, QLatin1Char('0')));
}
