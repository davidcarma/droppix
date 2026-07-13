#pragma once
#include <QVideoWidget>
#include <functional>
#include <map>
#include <set>
#include <vector>
#include "protocol.h"   // droppix::TouchContact
#include "touch_normalize.h"

namespace droppix {

// Renders decoded video (via its inherited QVideoSink) and captures touch/mouse input,
// normalizing it into the same TouchContact wire shape the Android app sends — see
// android/.../ui/DisplaySurfaceView.kt for the algorithm this mirrors:
//   - a real touchscreen's QTouchEvent points map 1:1 to contacts (multi-touch works)
//   - without one, left-click+drag synthesizes a single contact (down/move/up)
//   - right/middle mouse buttons and the wheel are sent directly as dedicated wire
//     messages (MsgType::MouseButton / MsgType::Scroll, see protocol.h) rather than
//     synthesized as touch contacts.
//   - MOVE is throttled to ~12ms (~83Hz) so a drag can't flood the host; DOWN/UP/CANCEL
//     are never throttled (a dropped "up" would leave a phantom finger stuck down).
//   - coordinates normalize to the widget's current pixel size, into 0..65535; pressure
//     (real touch pressure if reported, else full-scale 1023 for mice/plain touchpads)
//     into 0..1023.
class VideoWidget : public QVideoWidget {
  Q_OBJECT
 public:
  explicit VideoWidget(QWidget* parent = nullptr);

  using TouchCallback = std::function<void(const std::vector<TouchContact>&)>;
  void setTouchCallback(TouchCallback cb) { onTouch_ = std::move(cb); }

  // dx/dy: wheel notches (angleDelta()/120); x/y: 0..65535 normalized pointer position.
  using ScrollCallback = std::function<void(int dx, int dy, uint16_t x, uint16_t y)>;
  void setScrollCallback(ScrollCallback cb) { scrollCb_ = std::move(cb); }

  // button: 1=right, 2=middle; action: 0=up, 1=down; x/y: 0..65535 normalized position.
  using MouseButtonCallback = std::function<void(uint8_t button, uint8_t action,
                                                 uint16_t x, uint16_t y)>;
  void setMouseButtonCallback(MouseButtonCallback cb) { mouseButtonCb_ = std::move(cb); }

  // keycode: evdev/linux input-event-codes.h scancode; action: 0=up, 1=down, 2=repeat.
  using KeyCallback = std::function<void(uint16_t keycode, uint8_t action)>;
  void setKeyCallback(KeyCallback cb) { keyCb_ = std::move(cb); }

 protected:
  bool event(QEvent* e) override;         // QTouchEvent path
  void mousePressEvent(QMouseEvent* e) override;
  void mouseMoveEvent(QMouseEvent* e) override;
  void mouseReleaseEvent(QMouseEvent* e) override;
  void wheelEvent(QWheelEvent* e) override;
  void keyPressEvent(QKeyEvent* e) override;
  void keyReleaseEvent(QKeyEvent* e) override;
  void focusOutEvent(QFocusEvent* e) override;

 private:
  void emitContacts(const std::vector<TouchContact>& contacts);
  TouchContact normalize(qreal x, qreal y, qreal pressure, uint8_t id) const {
    return normalize_touch(x, y, width(), height(), pressure, id);
  }

  TouchCallback onTouch_;
  ScrollCallback scrollCb_;
  MouseButtonCallback mouseButtonCb_;
  KeyCallback keyCb_;
  std::set<uint16_t> heldKeys_;
  qint64 lastMoveSentMs_ = 0;
  bool mouseDown_ = false;
  static constexpr qint64 kMoveMinIntervalMs = 12;
};

}  // namespace droppix
