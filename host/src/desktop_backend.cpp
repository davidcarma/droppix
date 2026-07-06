#include "desktop_backend.h"
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <pwd.h>

namespace droppix {

std::string user_session_prefix() {
  const char* uid = std::getenv("PKEXEC_UID");
  if (!uid || !*uid) uid = std::getenv("SUDO_UID");
  if (!uid || !*uid) return "env ";  // already in a user session
  const std::string u(uid);
  const std::string env =
      "env XDG_RUNTIME_DIR=/run/user/" + u + " "
      "DBUS_SESSION_BUS_ADDRESS=unix:path=/run/user/" + u + "/bus "
      "WAYLAND_DISPLAY=wayland-0 ";
  struct passwd* pw = getpwuid(static_cast<uid_t>(std::atoi(u.c_str())));
  if (pw && pw->pw_name) return std::string("runuser -u ") + pw->pw_name + " -- " + env;
  return "sudo -u '#" + u + "' " + env;
}

// Output names are short connector ids (DP-3, HDMI-A-3, ...); reject anything else
// so the name can be safely interpolated into the bind shell command.
bool safe_output_name(const std::string& s) {
  if (s.empty() || s.size() > 64) return false;
  for (char c : s) if (!std::isalnum((unsigned char)c) && c != '-' && c != '_') return false;
  return true;
}

std::vector<OutputInfo> KWinBackend::outputs() {
  std::string out;
  std::string cmd = "timeout 3 " + user_session_prefix() + "kscreen-doctor -o 2>/dev/null";
  FILE* p = popen(cmd.c_str(), "r");
  if (p) {
    char buf[4096]; size_t n;
    while ((n = fread(buf, 1, sizeof(buf), p)) > 0) out.append(buf, n);
    pclose(p);
  }
  std::fprintf(stderr, "input: kscreen query returned %zu bytes\n", out.size());
  return parse_kscreen_outputs(out);
}

// Map the droppix-touch device onto the droppix output via KWin's per-device DBus
// properties (mapToWorkspace=false + outputName=<droppix>). Reads/writes via
// org.freedesktop.DBus.Properties (the qdbus shorthand silently errors on these
// objects). Retries while KWin registers the new uinput device.
void KWinBackend::map_touch(const std::string& output_name, const std::string& touch_name) {
  if (!safe_output_name(output_name)) return;
  if (!safe_output_name(touch_name)) return;
  const std::string inner =
      "QD=; for q in qdbus6 qdbus-qt6 qdbus; do command -v \"$q\" >/dev/null 2>&1 && QD=$q && break; done; "
      "[ -z \"$QD\" ] && { echo \"[touch-bind] no qdbus available\" >&2; exit 0; }; "
      "I=org.kde.KWin.InputDevice; PG=org.freedesktop.DBus.Properties.Get; PS=org.freedesktop.DBus.Properties.Set; "
      "for i in 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15; do "
      "for d in $(\"$QD\" org.kde.KWin /org/kde/KWin/InputDevice "
      "org.kde.KWin.InputDeviceManager.ListTouch 2>/dev/null); do "
      "P=/org/kde/KWin/InputDevice/$d; "
      "n=$(\"$QD\" org.kde.KWin \"$P\" $PG $I name 2>/dev/null); "
      "if [ \"$n\" = " + touch_name + " ]; then "
      "echo \"[touch-bind] found droppix-touch ($d) before mapToWorkspace=$(\"$QD\" org.kde.KWin \"$P\" $PG $I mapToWorkspace 2>/dev/null) outputName=[$(\"$QD\" org.kde.KWin \"$P\" $PG $I outputName 2>/dev/null)] target=" +
      output_name + "\" >&2; "
      "\"$QD\" org.kde.KWin \"$P\" $PS $I mapToWorkspace false 2>&1 | sed \"s/^/[touch-bind] set mapToWorkspace: /\" >&2; "
      "\"$QD\" org.kde.KWin \"$P\" $PS $I outputName " + output_name +
      " 2>&1 | sed \"s/^/[touch-bind] set outputName: /\" >&2; "
      "echo \"[touch-bind] after mapToWorkspace=$(\"$QD\" org.kde.KWin \"$P\" $PG $I mapToWorkspace 2>/dev/null) outputName=[$(\"$QD\" org.kde.KWin \"$P\" $PG $I outputName 2>/dev/null)]\" >&2; "
      "exit 0; fi; done; sleep 0.2; done; "
      "echo \"[touch-bind] droppix-touch not found via ListTouch after retries\" >&2";
  std::string cmd = "timeout 10 " + user_session_prefix() + "sh -c '" + inner + "'";
  std::system(cmd.c_str());
}

void GenericBackend::map_touch(const std::string& output, const std::string& touch_dev) {
  (void)output; (void)touch_dev;
  std::fprintf(stderr, "input: touch-to-output mapping not supported on this desktop yet; "
                       "display works, touch not bound\n");
}

BackendKind select_backend_kind(const std::string& xdg_current_desktop, bool has_kscreen) {
  std::string d;
  for (char c : xdg_current_desktop) d.push_back(static_cast<char>(std::tolower((unsigned char)c)));
  if (d.find("kde") != std::string::npos || d.find("plasma") != std::string::npos)
    return BackendKind::KWin;
  if (d.empty() && has_kscreen) return BackendKind::KWin;
  return BackendKind::Generic;
}

std::shared_ptr<DesktopBackend> make_desktop_backend() {
  const char* xdg = std::getenv("XDG_CURRENT_DESKTOP");
  const std::string desktop = xdg ? xdg : "";
  const bool has_kscreen = std::system("command -v kscreen-doctor >/dev/null 2>&1") == 0;
  std::shared_ptr<DesktopBackend> b;
  if (select_backend_kind(desktop, has_kscreen) == BackendKind::KWin)
    b = std::make_shared<KWinBackend>();
  else
    b = std::make_shared<GenericBackend>();
  std::fprintf(stderr, "desktop backend: %s\n", b->name());
  return b;
}

}  // namespace droppix
