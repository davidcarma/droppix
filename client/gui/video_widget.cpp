#include "video_widget.h"
#include <QGuiApplication>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QTouchEvent>
#include <QWheelEvent>
#include <QDateTime>
#include <algorithm>
#include "keycode_util.h"

namespace droppix {

VideoWidget::VideoWidget(QWidget* parent) : QVideoWidget(parent) {
  setAttribute(Qt::WA_AcceptTouchEvents, true);
  setFocusPolicy(Qt::StrongFocus);
}

void VideoWidget::emitContacts(const std::vector<TouchContact>& contacts) {
  if (onTouch_) onTouch_(contacts);
}

bool VideoWidget::event(QEvent* e) {
  switch (e->type()) {
    case QEvent::TouchBegin:
    case QEvent::TouchUpdate:
    case QEvent::TouchEnd:
    case QEvent::TouchCancel: {
      auto* te = static_cast<QTouchEvent*>(e);
      if (e->type() == QEvent::TouchCancel) { emitContacts({}); return true; }

      const auto& points = te->points();
      const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
      const bool isUpdate = e->type() == QEvent::TouchUpdate;
      bool anyNonMove = false;
      for (const auto& p : points)
        if (p.state() != QEventPoint::Updated) anyNonMove = true;
      // Coalesce a pure-move burst (mirrors DisplaySurfaceView.kt's ~12ms MOVE throttle);
      // any press/release is always sent immediately regardless of timing.
      if (isUpdate && !anyNonMove) {
        if (nowMs - lastMoveSentMs_ < kMoveMinIntervalMs) return true;
        lastMoveSentMs_ = nowMs;
      }
      std::vector<TouchContact> contacts;
      for (const auto& p : points) {
        if (p.state() == QEventPoint::Released) continue;   // exclude the lifting finger
        contacts.push_back(normalize(p.position().x(), p.position().y(),
                                     p.pressure() > 0 ? p.pressure() : 1.0, p.id() & 0xFF));
      }
      emitContacts(contacts);
      return true;
    }
    default:
      return QVideoWidget::event(e);
  }
}

namespace {
// Wire button codes for MsgType::MouseButton (see protocol.h): 1=right, 2=middle.
uint8_t wireButton(Qt::MouseButton b) {
  return b == Qt::RightButton ? 1 : 2;
}
}  // namespace

void VideoWidget::mousePressEvent(QMouseEvent* e) {
  if (e->button() == Qt::RightButton || e->button() == Qt::MiddleButton) {
    if (mouseButtonCb_) {
      auto n = normalize(e->position().x(), e->position().y(), 1.0, 0);
      mouseButtonCb_(wireButton(e->button()), 1, n.x, n.y);
    }
    return;
  }
  if (e->button() == Qt::LeftButton) {
    mouseDown_ = true;
    emitContacts({normalize(e->position().x(), e->position().y(), 1.0, 0)});
  }
}

void VideoWidget::mouseMoveEvent(QMouseEvent* e) {
  if (!mouseDown_) return;
  const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
  if (nowMs - lastMoveSentMs_ < kMoveMinIntervalMs) return;
  lastMoveSentMs_ = nowMs;
  emitContacts({normalize(e->position().x(), e->position().y(), 1.0, 0)});
}

void VideoWidget::mouseReleaseEvent(QMouseEvent* e) {
  if (e->button() == Qt::RightButton || e->button() == Qt::MiddleButton) {
    if (mouseButtonCb_) {
      auto n = normalize(e->position().x(), e->position().y(), 1.0, 0);
      mouseButtonCb_(wireButton(e->button()), 0, n.x, n.y);
    }
    return;
  }
  if (e->button() != Qt::LeftButton) return;
  mouseDown_ = false;
  emitContacts({});
}

void VideoWidget::wheelEvent(QWheelEvent* e) {
  QPoint d = e->angleDelta();
  int dx = d.x() / 120;
  int dy = d.y() / 120;
  if (scrollCb_ && (dx || dy)) {
    auto n = normalize(e->position().x(), e->position().y(), 1.0, 0);
    scrollCb_(dx, dy, n.x, n.y);
  }
  e->accept();
}

// X11 (xcb) scancode = evdev keycode + 8 (the kernel evdev codes start at 1, but X reserves
// the first 8 for pseudo-keys); Wayland scancodes are already raw evdev codes (no offset).
// scancode_to_evdev() branches on the active QPA platform so both platforms send correct
// codes; it returns 0 when there's no usable evdev mapping (e.g. a synthetic/virtual key
// event), which is ignored rather than forwarded as garbage.
void VideoWidget::keyPressEvent(QKeyEvent* e) {
  bool wayland = QGuiApplication::platformName() == QLatin1String("wayland");
  int evdev = droppix::scancode_to_evdev(static_cast<int>(e->nativeScanCode()), wayland);
  if (evdev == 0) { e->ignore(); return; }
  if (keyCb_) keyCb_(static_cast<uint16_t>(evdev), e->isAutoRepeat() ? 2 : 1);
  e->accept();
}

void VideoWidget::keyReleaseEvent(QKeyEvent* e) {
  if (e->isAutoRepeat()) { e->accept(); return; }   // autorepeat release is an artifact, not a real up
  bool wayland = QGuiApplication::platformName() == QLatin1String("wayland");
  int evdev = droppix::scancode_to_evdev(static_cast<int>(e->nativeScanCode()), wayland);
  if (evdev == 0) { e->ignore(); return; }
  if (keyCb_) keyCb_(static_cast<uint16_t>(evdev), 0);
  e->accept();
}

}  // namespace droppix
