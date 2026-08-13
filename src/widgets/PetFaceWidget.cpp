#include "PetFaceWidget.h"

#include "VisualTokens.h"

#include <QHideEvent>
#include <QPainter>
#include <QPainterPath>
#include <QRegion>
#include <QResizeEvent>
#include <QShowEvent>

#include <array>

namespace {
QPen facePen(const QColor& color, qreal width)
{
    QPen pen(color, width, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    return pen;
}

void drawSmile(QPainter& painter, qreal left, qreal right, qreal y, qreal depth)
{
    QPainterPath path;
    path.moveTo(left, y);
    path.quadTo((left + right) / 2.0, y + depth, right, y);
    painter.drawPath(path);
}

void drawFrown(QPainter& painter, qreal left, qreal right, qreal y, qreal height)
{
    QPainterPath path;
    path.moveTo(left, y);
    path.quadTo((left + right) / 2.0, y - height, right, y);
    painter.drawPath(path);
}

void drawTiltedEye(QPainter& painter, const QPointF& from, const QPointF& to)
{
    painter.drawLine(from, to);
}

void drawPlayfulWink(QPainter& painter)
{
    QPainterPath eye;
    eye.moveTo(326, 116);
    eye.cubicTo(348, 80, 381, 70, 416, 82);
    painter.drawPath(eye);
}

void drawSleepMouth(QPainter& painter)
{
    QPainterPath mouth;
    mouth.moveTo(206, 188);
    mouth.cubicTo(232, 203, 249, 203, 270, 188);
    mouth.cubicTo(289, 175, 304, 182, 326, 188);
    painter.drawPath(mouth);
}

void drawSleepBubble(QPainter& painter, const QColor& color, bool compact)
{
    painter.save();
    painter.setPen(facePen(color, compact ? 8.0 : 10.0));
    painter.setBrush(Qt::NoBrush);
    painter.drawRoundedRect(QRectF(382, 12, 112, 70), 34, 34);

    QPainterPath tail;
    tail.moveTo(414, 76);
    tail.lineTo(404, 94);
    tail.lineTo(432, 80);
    painter.drawPath(tail);

    QFont font = painter.font();
    font.setPixelSize(38);
    font.setWeight(QFont::DemiBold);
    painter.setFont(font);
    painter.drawText(QRectF(400, 21, 74, 48), Qt::AlignCenter, QStringLiteral("Z"));
    painter.restore();
}

const QPainterPath& listeningWavePath(bool rightSide, int index)
{
    static const std::array<QPainterPath, 4> paths = [] {
        std::array<QPainterPath, 4> result;
        for (int i = 0; i < 2; ++i) {
            result[i].moveTo(82 - i * 24, 76 - i * 12);
            result[i].quadTo(48 - i * 18, 112, 82 - i * 24, 148 + i * 12);
            result[i + 2].moveTo(430 + i * 24, 76 - i * 12);
            result[i + 2].quadTo(464 + i * 18, 112, 430 + i * 24, 148 + i * 12);
        }
        return result;
    }();
    return paths[(rightSide ? 2 : 0) + index];
}
}

PetFaceWidget::PetFaceWidget(PetExpression expression, QWidget* parent)
    : QWidget(parent),
      m_expression(expression),
      m_backgroundColor(expression == PetExpression::Sleep
          ? LongPetUi::Colors::SleepBackground : LongPetUi::Colors::BackgroundPrimary)
{
    setAttribute(Qt::WA_OpaquePaintEvent, true);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    m_animationTimer.setTimerType(Qt::CoarseTimer);
    connect(&m_animationTimer, &QTimer::timeout,
            this, &PetFaceWidget::advanceAnimationPhase);
    m_blinkTimer.setSingleShot(true);
    m_blinkTimer.setTimerType(Qt::VeryCoarseTimer);
    connect(&m_blinkTimer, &QTimer::timeout, this, &PetFaceWidget::handleBlinkTimer);
}

PetExpression PetFaceWidget::expression() const { return m_expression; }

void PetFaceWidget::setExpression(PetExpression expression)
{
    if (m_expression == expression)
        return;
    m_expression = expression;
    m_animationPhase = 0;
    m_blinking = false;
    invalidateCache();
    syncTimers();
    update();
}

void PetFaceWidget::setAnimationEnabled(bool enabled)
{
    if (m_animationEnabled == enabled)
        return;
    m_animationEnabled = enabled;
    m_animationPhase = 0;
    m_blinking = false;
    syncTimers();
    update();
}

bool PetFaceWidget::animationEnabled() const { return m_animationEnabled; }

void PetFaceWidget::setCompact(bool compact)
{
    if (m_compact == compact)
        return;
    m_compact = compact;
    invalidateCache();
    update();
}

void PetFaceWidget::setBackgroundColor(const QColor& color)
{
    if (m_backgroundColor == color)
        return;
    m_backgroundColor = color;
    invalidateCache();
    update();
}

QString PetFaceWidget::expressionName(PetExpression expression)
{
    switch (expression) {
    case PetExpression::Default: return QStringLiteral("默认");
    case PetExpression::DefaultOpen: return QStringLiteral("睁眼默认");
    case PetExpression::Playful: return QStringLiteral("俏皮");
    case PetExpression::Happy: return QStringLiteral("高兴");
    case PetExpression::Worried: return QStringLiteral("担忧");
    case PetExpression::Angry: return QStringLiteral("生气");
    case PetExpression::Sleep: return QStringLiteral("睡眠");
    case PetExpression::Listening: return QStringLiteral("倾听");
    case PetExpression::Thinking: return QStringLiteral("思考");
    case PetExpression::Speaking: return QStringLiteral("说话");
    case PetExpression::Alert: return QStringLiteral("警觉");
    case PetExpression::CuteCat: return QStringLiteral("猫猫脸");
    }
    return {};
}

QString PetFaceWidget::expressionNameEn(PetExpression expression)
{
    switch (expression) {
    case PetExpression::Default: return QStringLiteral("Default");
    case PetExpression::DefaultOpen: return QStringLiteral("Default 2");
    case PetExpression::Playful: return QStringLiteral("Playful");
    case PetExpression::Happy: return QStringLiteral("Happy");
    case PetExpression::Worried: return QStringLiteral("Worried");
    case PetExpression::Angry: return QStringLiteral("Angry");
    case PetExpression::Sleep: return QStringLiteral("Sleep");
    case PetExpression::Listening: return QStringLiteral("Listening");
    case PetExpression::Thinking: return QStringLiteral("Thinking");
    case PetExpression::Speaking: return QStringLiteral("Speaking");
    case PetExpression::Alert: return QStringLiteral("Alert");
    case PetExpression::CuteCat: return QStringLiteral("Cute Cat");
    }
    return {};
}

void PetFaceWidget::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    syncTimers();
}

void PetFaceWidget::hideEvent(QHideEvent* event)
{
    QWidget::hideEvent(event);
    m_animationTimer.stop();
    m_blinkTimer.stop();
    m_blinking = false;
}

void PetFaceWidget::resizeEvent(QResizeEvent* event)
{
    invalidateCache();
    QWidget::resizeEvent(event);
}

bool PetFaceWidget::usesContinuousAnimation() const
{
    return m_expression == PetExpression::Listening
        || m_expression == PetExpression::Thinking
        || m_expression == PetExpression::Speaking;
}

bool PetFaceWidget::supportsBlink() const
{
    return m_expression == PetExpression::Default
        || m_expression == PetExpression::DefaultOpen
        || m_expression == PetExpression::Listening
        || m_expression == PetExpression::Speaking;
}

int PetFaceWidget::animationIntervalMs() const
{
    switch (m_expression) {
    case PetExpression::Listening: return 240; // 约 4.2 FPS
    case PetExpression::Thinking: return 320;  // 约 3.1 FPS
    case PetExpression::Speaking: return 220;  // 约 4.5 FPS
    default: return 0;
    }
}

int PetFaceWidget::animationPhaseCount() const
{
    switch (m_expression) {
    case PetExpression::Listening: return 2;
    case PetExpression::Thinking: return 3;
    case PetExpression::Speaking: return 4;
    default: return 1;
    }
}

void PetFaceWidget::syncTimers()
{
    m_animationTimer.stop();
    m_blinkTimer.stop();
    if (!m_animationEnabled || !isVisible())
        return;

    if (usesContinuousAnimation())
        m_animationTimer.start(animationIntervalMs());
    if (supportsBlink())
        scheduleNextBlink();
}

void PetFaceWidget::scheduleNextBlink()
{
    static constexpr int delaysMs[] = {4800, 6200, 5400, 6900};
    const int delay = delaysMs[m_blinkScheduleIndex
        % (sizeof(delaysMs) / sizeof(delaysMs[0]))];
    ++m_blinkScheduleIndex;
    m_blinkTimer.setTimerType(Qt::CoarseTimer);
    m_blinkTimer.start(delay);
}

void PetFaceWidget::handleBlinkTimer()
{
    if (!m_animationEnabled || !isVisible() || !supportsBlink())
        return;

    m_blinking = !m_blinking;
    updateBlinkRegion();
    if (m_blinking) {
        m_blinkTimer.setTimerType(Qt::PreciseTimer);
        m_blinkTimer.start(150);
    } else {
        scheduleNextBlink();
    }
}

void PetFaceWidget::advanceAnimationPhase()
{
    if (!m_animationEnabled || !isVisible() || !usesContinuousAnimation()) {
        m_animationTimer.stop();
        return;
    }
    m_animationPhase = (m_animationPhase + 1) % animationPhaseCount();
    updateDynamicRegion();
}

void PetFaceWidget::invalidateCache()
{
    m_staticCache = QImage();
    m_blinkCache = QImage();
    m_cachedLogicalSize = QSize();
    m_cachedDevicePixelRatio = 0.0;
}

QImage PetFaceWidget::renderStaticCache(bool blink) const
{
    const qreal dpr = devicePixelRatioF();
    const QSize pixelSize(qMax(1, qRound(width() * dpr)),
                          qMax(1, qRound(height() * dpr)));
    QImage image(pixelSize, QImage::Format_ARGB32_Premultiplied);
    image.setDevicePixelRatio(dpr);
    image.fill(m_backgroundColor);
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing, true);
    applyLogicalTransform(painter);
    drawStaticExpression(painter, blink);
    return image;
}

void PetFaceWidget::ensureStaticCache(bool blink)
{
    const qreal dpr = devicePixelRatioF();
    if (m_cachedLogicalSize != size() || !qFuzzyCompare(m_cachedDevicePixelRatio, dpr)) {
        invalidateCache();
        m_cachedLogicalSize = size();
        m_cachedDevicePixelRatio = dpr;
    }
    QImage& cache = blink ? m_blinkCache : m_staticCache;
    if (cache.isNull())
        cache = renderStaticCache(blink);
}

void PetFaceWidget::applyLogicalTransform(QPainter& painter) const
{
    const qreal scale = qMin(width() / 512.0, height() / 256.0);
    painter.translate(width() / 2.0, height() / 2.0);
    painter.scale(scale, scale);
    painter.translate(-256.0, -128.0);
}

QRect PetFaceWidget::mapLogicalRect(const QRectF& logicalRect, qreal logicalPadding) const
{
    const qreal scale = qMin(width() / 512.0, height() / 256.0);
    const qreal offsetX = (width() - 512.0 * scale) / 2.0;
    const qreal offsetY = (height() - 256.0 * scale) / 2.0;
    const QRectF padded = logicalRect.adjusted(-logicalPadding, -logicalPadding,
                                               logicalPadding, logicalPadding);
    return QRect(qFloor(offsetX + padded.left() * scale),
                 qFloor(offsetY + padded.top() * scale),
                 qCeil(padded.width() * scale),
                 qCeil(padded.height() * scale)).intersected(rect());
}

void PetFaceWidget::updateDynamicRegion()
{
    switch (m_expression) {
    case PetExpression::Listening: {
        QRegion waves(mapLogicalRect(QRectF(28, 44, 78, 138), 12));
        waves += mapLogicalRect(QRectF(406, 44, 78, 138), 12);
        update(waves);
        break;
    }
    case PetExpression::Thinking:
        update(mapLogicalRect(QRectF(408, 0, 104, 92), 12));
        break;
    case PetExpression::Speaking:
        update(mapLogicalRect(QRectF(310, 150, 118, 72), 12));
        break;
    default:
        break;
    }
}

void PetFaceWidget::updateBlinkRegion()
{
    update(mapLogicalRect(QRectF(96, 42, 320, 126), 12));
}

void PetFaceWidget::paintEvent(QPaintEvent*)
{
    ensureStaticCache(m_blinking);
    QPainter painter(this);
    painter.drawImage(QPoint(0, 0), m_blinking ? m_blinkCache : m_staticCache);
    if (usesContinuousAnimation()) {
        painter.setRenderHint(QPainter::Antialiasing, true);
        applyLogicalTransform(painter);
        drawDynamicOverlay(painter);
    }
}

void PetFaceWidget::drawOpenEye(QPainter& painter, const QPointF& center, const QSizeF& size,
                                qreal pupilOffsetX, bool blink) const
{
    if (blink) {
        drawClosedEye(painter, center.x(), center.y(), size.width() * 0.55, false);
        return;
    }

    const QColor color = painter.pen().color();
    painter.setPen(Qt::NoPen);
    painter.setBrush(color);
    painter.drawRoundedRect(QRectF(center.x() - size.width() / 2.0 + pupilOffsetX,
                                   center.y() - size.height() / 2.0,
                                   size.width(), size.height()),
                            size.width() / 2.0, size.width() / 2.0);
    painter.setBrush(Qt::NoBrush);
    painter.setPen(facePen(color, m_compact ? 22.0 : 30.0));
}

void PetFaceWidget::drawClosedEye(QPainter& painter, qreal centerX, qreal centerY,
                                  qreal halfWidth, bool happyArc) const
{
    QPainterPath eye;
    eye.moveTo(centerX - halfWidth, centerY + (happyArc ? 8.0 : -8.0));
    eye.quadTo(centerX, centerY + (happyArc ? -24.0 : 28.0),
                centerX + halfWidth, centerY + (happyArc ? 8.0 : -8.0));
    painter.drawPath(eye);
}

void PetFaceWidget::drawStaticExpression(QPainter& painter, bool blink) const
{
    const QColor faceColor = m_expression == PetExpression::Sleep
        ? LongPetUi::Colors::SleepContent : LongPetUi::Colors::TextPrimary;
    const qreal mainWidth = m_compact ? 22.0 : 30.0;
    painter.setPen(facePen(faceColor, mainWidth));
    painter.setBrush(Qt::NoBrush);

    switch (m_expression) {
    case PetExpression::Default:
        if (blink) {
            drawClosedEye(painter, 156, 96, 42, false);
            drawClosedEye(painter, 356, 96, 42, false);
        } else {
            drawTiltedEye(painter, {118, 108}, {188, 76});
            drawTiltedEye(painter, {324, 76}, {394, 108});
        }
        drawSmile(painter, 158, 354, 160, 92);
        break;
    case PetExpression::DefaultOpen:
        drawOpenEye(painter, {156, 100}, {54, 82}, 0, blink);
        drawOpenEye(painter, {356, 100}, {54, 82}, 0, blink);
        painter.setPen(facePen(faceColor, m_compact ? 18.0 : 24.0));
        drawSmile(painter, 210, 302, 176, 30);
        break;
    case PetExpression::Playful:
        drawOpenEye(painter, {156, 98}, {54, 82}, 0, false);
        drawPlayfulWink(painter);
        drawSmile(painter, 190, 336, 172, 72);
        break;
    case PetExpression::Happy:
        drawClosedEye(painter, 156, 102, 46, true);
        drawClosedEye(painter, 356, 102, 46, true);
        drawSmile(painter, 146, 366, 164, 76);
        break;
    case PetExpression::Worried: {
        painter.setPen(facePen(faceColor, m_compact ? 13.0 : 17.0));
        QPainterPath leftBrow;
        leftBrow.moveTo(128, 72);
        leftBrow.quadTo(154, 56, 188, 62);
        painter.drawPath(leftBrow);
        QPainterPath rightBrow;
        rightBrow.moveTo(324, 62);
        rightBrow.quadTo(358, 56, 384, 72);
        painter.drawPath(rightBrow);
        painter.setPen(facePen(faceColor, mainWidth));
        drawOpenEye(painter, {160, 116}, {58, 84}, 0, false);
        drawOpenEye(painter, {352, 116}, {58, 84}, 0, false);
        painter.setPen(facePen(faceColor, m_compact ? 13.0 : 17.0));
        drawFrown(painter, 232, 280, 207, 10);
        break;
    }
    case PetExpression::Angry: {
        drawOpenEye(painter, {156, 112}, {52, 76}, 0, false);
        drawOpenEye(painter, {356, 112}, {52, 76}, 0, false);
        painter.setPen(facePen(faceColor, m_compact ? 15.0 : 20.0));
        drawTiltedEye(painter, {126, 62}, {188, 84});
        drawTiltedEye(painter, {324, 84}, {386, 62});
        QPainterPath angryMouth;
        angryMouth.moveTo(226, 204);
        angryMouth.quadTo(256, 178, 286, 204);
        painter.drawPath(angryMouth);
        break;
    }
    case PetExpression::Sleep:
        drawClosedEye(painter, 150, 112, 43, false);
        drawClosedEye(painter, 342, 112, 43, false);
        painter.setPen(facePen(faceColor, m_compact ? 17.0 : 22.0));
        drawSleepMouth(painter);
        drawSleepBubble(painter, faceColor, m_compact);
        break;
    case PetExpression::Listening: {
        drawOpenEye(painter, {156, 102}, {54, 82}, 0, blink);
        drawOpenEye(painter, {356, 102}, {54, 82}, 0, blink);
        painter.setPen(facePen(faceColor, m_compact ? 18.0 : 24.0));
        drawSmile(painter, 212, 300, 180, 34);
        break;
    }
    case PetExpression::Thinking: {
        drawOpenEye(painter, {156, 104}, {54, 82}, 4, false);
        painter.setPen(facePen(faceColor, mainWidth));
        QPainterPath thinkingEye;
        thinkingEye.moveTo(320, 112);
        thinkingEye.cubicTo(346, 78, 378, 72, 410, 84);
        painter.drawPath(thinkingEye);
        painter.setPen(facePen(faceColor, m_compact ? 17.0 : 22.0));
        painter.drawLine(QPointF(232, 194), QPointF(280, 194));
        break;
    }
    case PetExpression::Speaking: {
        drawOpenEye(painter, {156, 100}, {54, 82}, -2, blink);
        drawOpenEye(painter, {356, 100}, {54, 82}, 2, blink);
        painter.setPen(facePen(faceColor, m_compact ? 16.0 : 21.0));
        QPainterPath speakingMouth;
        speakingMouth.moveTo(220, 166);
        speakingMouth.cubicTo(220, 198, 246, 204, 256, 178);
        speakingMouth.cubicTo(266, 204, 292, 198, 292, 166);
        painter.drawPath(speakingMouth);
        break;
    }
    case PetExpression::Alert:
        drawOpenEye(painter, {156, 100}, {54, 82}, 0, false);
        drawOpenEye(painter, {356, 100}, {54, 82}, 0, false);
        painter.setPen(facePen(faceColor, m_compact ? 18.0 : 24.0));
        painter.drawEllipse(QRectF(228, 166, 56, 56));
        painter.setPen(facePen(LongPetUi::Colors::Danger, m_compact ? 10.0 : 14.0));
        painter.drawLine(QPointF(76, 74), QPointF(64, 112));
        painter.drawLine(QPointF(64, 112), QPointF(76, 150));
        painter.drawLine(QPointF(436, 74), QPointF(448, 112));
        painter.drawLine(QPointF(448, 112), QPointF(436, 150));
        break;
    case PetExpression::CuteCat: {
        drawOpenEye(painter, {156, 100}, {52, 78}, 0, false);
        drawOpenEye(painter, {356, 100}, {52, 78}, 0, false);
        painter.setPen(facePen(faceColor, m_compact ? 15.0 : 20.0));
        QPainterPath catMouth;
        catMouth.moveTo(224, 164);
        catMouth.cubicTo(224, 190, 246, 196, 256, 176);
        catMouth.cubicTo(266, 196, 288, 190, 288, 164);
        painter.drawPath(catMouth);
        painter.setPen(facePen(LongPetUi::Colors::Accent, m_compact ? 8.0 : 11.0));
        painter.drawLine(QPointF(194, 184), QPointF(150, 174));
        painter.drawLine(QPointF(318, 184), QPointF(362, 174));
        break;
    }
    }
}

void PetFaceWidget::drawDynamicOverlay(QPainter& painter) const
{
    switch (m_expression) {
    case PetExpression::Listening: {
        painter.setPen(facePen(LongPetUi::Colors::Accent, m_compact ? 10.0 : 14.0));
        for (int i = 0; i < 2; ++i) {
            painter.setOpacity(i == m_animationPhase ? 1.0 : 0.55);
            painter.drawPath(listeningWavePath(false, i));
            painter.drawPath(listeningWavePath(true, i));
        }
        painter.setOpacity(1.0);
        break;
    }
    case PetExpression::Thinking:
        painter.setPen(Qt::NoPen);
        for (int i = 0; i < 3; ++i) {
            QColor dot = LongPetUi::Colors::Accent;
            dot.setAlpha(i == m_animationPhase ? 255 : 110);
            painter.setBrush(dot);
            painter.drawEllipse(QPointF(420 + i * 28, 54 - i * 18),
                                5 + i * 2, 5 + i * 2);
        }
        break;
    case PetExpression::Speaking: {
        static constexpr int lengths[4] = {0, 1, 2, 1};
        const int step = lengths[m_animationPhase];
        painter.setPen(facePen(LongPetUi::Colors::Accent, m_compact ? 9.0 : 13.0));
        painter.drawLine(QPointF(322, 170), QPointF(344 + step * 5, 170));
        painter.drawLine(QPointF(322, 188), QPointF(364 + step * 8, 188));
        painter.drawLine(QPointF(322, 206), QPointF(386 + step * 10, 206));
        break;
    }
    default:
        break;
    }
}
