#pragma once
#include <QMainWindow>
#include <QHash>
#include <QString>
#include <functional>
#include "settings.h"
#include "profile_store.h"
#include "stream_controller.h"
#include "session_manager.h"
#include "adb_manager.h"
#include "mdns_advertiser.h"
#include "mdns_browser.h"
#include "usb_client_scanner.h"
#include "approved_store.h"
#include "cert_manager.h"
#include "audio_sink.h"

class QComboBox; class QSpinBox; class QCheckBox; class QPushButton;
class QLabel; class QPlainTextEdit; class QRadioButton; class QTimer;
class QListWidget; class QGroupBox; class QSystemTrayIcon; class QDialog;

namespace droppix {
class SettingsDialog;
class MainWindow : public QMainWindow {
  Q_OBJECT
 public:
  explicit MainWindow(QWidget* parent = nullptr);
 protected:
  void closeEvent(QCloseEvent* event) override;
 private:
  std::string resolveStreamBin();   // sibling binary, extracted AppImage copy, or host-staged (Flatpak)
  void stageCertsToHost();           // Flatpak: mirror cert/key to the host for the streamer
  Settings collectSettings() const;
  void applySettings(const Settings& s);
  void onStartStop();           // Start button -> spawn a session on the next free port
  // Spawn a streaming session: a new StreamController on `port`, wired, started, added to
  // the Active-monitors panel; `directTablet` (may be empty) wakes/usb-connects the tablet.
  void startSession(const QString& key, const QString& label, const QString& transport,
                    int port, std::function<void()> directTablet);
  void wireSession(StreamController* c, const QString& key);
  void stopSelectedMonitor();   // stop the session selected in the Active-monitors list
  void updateStatus();          // status dot/text from session count + connectivity
  void refreshProfiles();
  void restoreLastProfile();
  void setStatusDot(const char* color);
  void setupAuth();              // install the polkit rule via one pkexec prompt
  void showAbout();             // Help -> About developer-info dialog
  void onDevicesChanged(const QList<MdnsDevice>& devices);
  void onUsbClientsChanged(const QList<UsbClient>& clients);
  void rebuildClientList();     // merge netDevices_ + usbClients_ into devicesList_
  void onConnectToSelectedDevice();
  void refreshAdvertising();    // (re)publish _droppix._tcp for the current port; idempotent
  bool minimizeToTrayRequested() const;   // reads the <config>/minimize_on_close marker
  void setupTray();             // create the tray icon + Show/Quit menu (if a tray exists)
  void showPairingPopup(const QString& ip);   // pop the pairing code when a device connects
  void hidePairingPopup();
  void manageDevices();         // dialog to view/forget remembered (approved) devices

  // widgets — ALL stream options (source/resolution/touch/audio/fps/bitrate/port/
  // refresh/orientation/auto-adb/overlay) now live in SettingsDialog (gear icon).
  SettingsDialog* settingsDialog_;
  QComboBox* profileBox_; QPushButton* startBtn_;
  QLabel* statusDot_;
  QLabel* deviceLabel_; QLabel* streamLabel_; QLabel* statsLabel_;
  QGroupBox* devicesBox_;
  QDialog* pairingPopup_ = nullptr;   // non-modal "Pairing code: NNNNNN" shown on connect
  QLabel* pairingInfo_ = nullptr;
  QLabel* pairingCodeLabel_ = nullptr;
  QTimer* pairingHideTimer_ = nullptr;
  QListWidget* devicesList_;
  QPushButton* connectBtn_;
  QGroupBox* monitorsBox_;      // "Active monitors" panel
  QListWidget* monitorsList_;   // one row per live session
  bool anyConnected_ = false;   // any session has a client connected (drives the status dot)

  ProfileStore store_;
  ApprovedStore approved_;
  CertManager cert_;
  DroppixAudioSink audioSink_;
  SessionManager sessions_;     // one session (= streamer = monitor) per connected tablet
  AdbManager adb_;
  MdnsAdvertiser advertiser_;
  quint16 advertisedPort_ = 0;     // port currently published via _droppix._tcp (0 = none)
  MdnsBrowser browser_;
  UsbClientScanner usbScanner_;
  QList<MdnsDevice> netDevices_;   // last network-discovered clients
  QList<UsbClient> usbClients_;    // last USB-discovered clients
  QHash<QString, qint64> pendingWakes_;
  QString flatpakHostRuntime_;         // Flatpak: host dir the streamer runtime is staged to
  QString flatpakHostCert_, flatpakHostKey_;   // Flatpak: host cert/key paths for the streamer
  QSystemTrayIcon* tray_ = nullptr;   // present only if a system tray is available
  bool quitting_ = false;             // true => closeEvent really quits (from tray Quit)
  bool trayHintShown_ = false;        // show the "still running" balloon only once
  std::string streamBin_;
};
}  // namespace droppix
