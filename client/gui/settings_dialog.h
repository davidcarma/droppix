#pragma once
#include <QDialog>
#include "client_settings.h"

class QComboBox;
class QCheckBox;

namespace droppix {

// Lets the user edit ClientSettings (resolution/fps/audio/rotation) sent to the host in
// HELLO. Resolution offers a "Native" entry (this device's current screen size, shown for
// context only — width/height 0 means "resolve at connect time") plus a few fixed presets.
class ClientSettingsDialog : public QDialog {
  Q_OBJECT
 public:
  ClientSettingsDialog(const ClientSettings& current, const QString& nativeLabel,
                       QWidget* parent = nullptr);
  ClientSettings result() const;

 private:
  QComboBox* resolution_;
  QComboBox* fps_;
  QCheckBox* audio_;
  QComboBox* rotation_;
};

}  // namespace droppix
