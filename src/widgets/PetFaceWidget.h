#pragma once

#include <QColor>
#include <QImage>
#include <QTimer>
#include <QWidget>

class QPainter;
class QResizeEvent;

enum class PetExpression {
    Default,
    DefaultOpen,
    Playful,
    Happy,
    Worried,
    Angry,
    Sleep,
    Listening,
    Thinking,
    Speaking,
    Alert,
    CuteCat
};

class PetFaceWidget final : public QWidget {
    Q_OBJECT

public:
    explicit PetFaceWidget(PetExpression expression = PetExpression::Default,
                           QWidget* parent = nullptr);

    PetExpression expression() const;
    void setExpression(PetExpression expression);
    void setAnimationEnabled(bool enabled);
    bool animationEnabled() const;
    void setCompact(bool compact);
    void setBackgroundColor(const QColor& color);

    static QString expressionName(PetExpression expression);
    static QString expressionNameEn(PetExpression expression);

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;

private:
    void syncTimers();
    void scheduleNextBlink();
    void advanceAnimationPhase();
    void handleBlinkTimer();
    bool usesContinuousAnimation() const;
    bool supportsBlink() const;
    int animationIntervalMs() const;
    int animationPhaseCount() const;
    void invalidateCache();
    void ensureStaticCache(bool blink);
    QImage renderStaticCache(bool blink) const;
    void applyLogicalTransform(QPainter& painter) const;
    QRect mapLogicalRect(const QRectF& logicalRect, qreal logicalPadding = 0.0) const;
    void updateDynamicRegion();
    void updateBlinkRegion();
    void drawOpenEye(QPainter& painter, const QPointF& center, const QSizeF& size,
                     qreal pupilOffsetX = 0.0, bool blink = false) const;
    void drawClosedEye(QPainter& painter, qreal centerX, qreal centerY,
                       qreal halfWidth, bool happyArc) const;
    void drawStaticExpression(QPainter& painter, bool blink) const;
    void drawDynamicOverlay(QPainter& painter) const;

    PetExpression m_expression;
    QTimer m_animationTimer;
    QTimer m_blinkTimer;
    QImage m_staticCache;
    QImage m_blinkCache;
    QSize m_cachedLogicalSize;
    qreal m_cachedDevicePixelRatio = 0.0;
    int m_animationPhase = 0;
    int m_blinkScheduleIndex = 0;
    bool m_blinking = false;
    bool m_animationEnabled = true;
    bool m_compact = false;
    QColor m_backgroundColor;
};
