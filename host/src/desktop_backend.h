#pragma once
#include <memory>
#include <string>
#include <vector>
#include "monitor_geometry.h"   // droppix::OutputInfo

namespace droppix {

// Session-command prefix so the root streamer can run user-session tools (kscreen,
// KWin DBus, pw-record) AS THE INVOKING USER via runuser + a reconstructed env.
// Returns "env " when already a user session. (Relocated from stream_daemon,
// unchanged: keeps WAYLAND_DISPLAY=wayland-0 — real-socket discovery is M2's job.)
std::string user_session_prefix();

// Validate a compositor output/connector name (connector-id chars only) before it is
//  interpolated into a shell command or trusted as identified. Shared by the KWin backend
//  and StreamDaemon's output-identification gate.
bool safe_output_name(const std::string& s);

// Per-desktop operations droppix needs beyond creating the evdi output. Compositing
// the virtual display, encode, and stream are compositor-agnostic and not here.
struct DesktopBackend {
  virtual ~DesktopBackend() = default;
  virtual const char* name() const = 0;                       // for logs: "kwin"/"generic"
  virtual std::vector<OutputInfo> outputs() = 0;              // enabled outputs w/ geometry
  virtual void map_touch(const std::string& output,
                         const std::string& touch_dev) = 0;   // best-effort; may no-op
};

// KDE Plasma: today's behavior, relocated (kscreen-doctor -o + KWin InputDevice DBus).
class KWinBackend : public DesktopBackend {
 public:
  const char* name() const override { return "kwin"; }
  std::vector<OutputInfo> outputs() override;
  void map_touch(const std::string& output, const std::string& touch_dev) override;
};

// Unknown/unsupported compositor: display still works (evdi is compositor-driven);
// outputs() returns {} and map_touch() logs a warning and no-ops.
class GenericBackend : public DesktopBackend {
 public:
  const char* name() const override { return "generic"; }
  std::vector<OutputInfo> outputs() override { return {}; }
  void map_touch(const std::string& output, const std::string& touch_dev) override;
};

enum class BackendKind { KWin, Generic };

// PURE (unit-tested). "kde"/"plasma" in the desktop string (case-insensitive) OR
// (empty desktop AND kscreen-doctor present) -> KWin; otherwise Generic.
BackendKind select_backend_kind(const std::string& xdg_current_desktop, bool has_kscreen);

// Detect the desktop (env + `command -v kscreen-doctor`), pick the backend, log it.
std::shared_ptr<DesktopBackend> make_desktop_backend();

}  // namespace droppix
