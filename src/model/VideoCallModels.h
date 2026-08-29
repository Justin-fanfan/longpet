#pragma once

#include <QDateTime>
#include <QMetaType>
#include <QString>

enum class VideoCallMode { Voice, Video };

enum class VideoCallDirection { DeviceToFamily, FamilyToDevice };

enum class VideoCallState {
    Idle,
    OutgoingRinging,
    NotifyingDevice,
    ConnectingMedia,
    Connected,
    Rejected,
    Ended,
    Failed
};

enum class VideoCallAction {
    Accept,
    Reject,
    HangUp,
    Fail
};

enum class VideoCallErrorCode {
    None,
    InvalidState,
    CallMismatch,
    RevisionConflict,
    Busy,
    MediaUnavailable,
    PermissionDenied
};

struct VideoCallSnapshot {
    QString callId;
    VideoCallState state = VideoCallState::Idle;
    VideoCallMode mode = VideoCallMode::Video;
    VideoCallDirection direction = VideoCallDirection::DeviceToFamily;
    QString remoteName = QStringLiteral("家属端");
    QDateTime startedAt;
    QDateTime connectedAt;
    QDateTime updatedAt;
    int revision = 0;
    bool mediaReady = false;
    quint16 mediaPort = 0;
    QString mediaToken;
    QString errorCode;
    QString errorMessage;

    bool isActive() const
    {
        return state == VideoCallState::OutgoingRinging
            || state == VideoCallState::NotifyingDevice
            || state == VideoCallState::ConnectingMedia
            || state == VideoCallState::Connected;
    }
};

struct VideoCallActionRequest {
    QString callId;
    VideoCallAction action = VideoCallAction::HangUp;
    int expectedRevision = 0;
    QString errorCode;
    QString errorMessage;
};

struct VideoCallResult {
    bool success = false;
    QString error;
    VideoCallErrorCode code = VideoCallErrorCode::None;
    VideoCallSnapshot snapshot;
};

Q_DECLARE_METATYPE(VideoCallSnapshot)
