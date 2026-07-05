#pragma once
#include <QObject>
#include <QUdpSocket>
#include <QTimer>
#include <QList>
#include <QString>
#include <QMetaType>

namespace droppix {

// A droppix tablet discovered over a (USB-tether) IP link via UDP probe/reply.
struct TetherClient {
  QString id;         // DeviceIdentity.stableId (cross-transport identity)
  QString name;       // display name
  QString address;    // tablet IP (reply source)
  quint16 wakePort = 0;
};

// Every ~2s broadcasts a "DPXQ" probe on each non-loopback interface and collects
// "DPXR" replies for a short window, emitting the discovered tablets. mDNS-independent,
// so it works across the USB-tether link where Android mDNS is unreliable. Replaces the
// adb-based UsbClientScanner.
class TetherScanner : public QObject {
  Q_OBJECT
 public:
  explicit TetherScanner(QObject* p = nullptr);
  bool available() const { return true; }   // no external tool needed
  void start();
  void stop();
 signals:
  void clientsChanged(QList<droppix::TetherClient> clients);
 private:
  void tick();                  // send probes, arm the collection window
  void onDatagram();            // parse an incoming reply
  QUdpSocket sock_;
  QTimer timer_;                // scan cadence
  QTimer window_;               // per-scan reply-collection window
  QList<TetherClient> acc_;     // replies this scan
};

}  // namespace droppix

Q_DECLARE_METATYPE(droppix::TetherClient)
