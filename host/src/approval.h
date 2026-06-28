#pragma once
#include <string>
#include <map>
#include <mutex>
#include <condition_variable>
#include <chrono>
namespace droppix {
inline bool parse_approval(const std::string& line, std::string& id, bool& allow) {
  auto sp = line.find(' ');
  if (sp == std::string::npos) return false;
  std::string cmd = line.substr(0, sp);
  id = line.substr(sp + 1);
  while (!id.empty() && (id.back()=='\n'||id.back()=='\r'||id.back()==' ')) id.pop_back();
  if (cmd == "approve") { allow = true;  return !id.empty(); }
  if (cmd == "deny")    { allow = false; return !id.empty(); }
  return false;
}
class ApprovalGate {
 public:
  void submit(const std::string& id, bool allow) {
    { std::lock_guard<std::mutex> lk(m_); decisions_[id] = allow; }
    cv_.notify_all();
  }
  bool wait(const std::string& id, int timeout_ms, bool& allow) {
    std::unique_lock<std::mutex> lk(m_);
    if (!cv_.wait_for(lk, std::chrono::milliseconds(timeout_ms),
                      [&]{ return decisions_.count(id) > 0; })) return false;
    allow = decisions_[id]; decisions_.erase(id); return true;
  }
 private:
  std::mutex m_; std::condition_variable cv_; std::map<std::string,bool> decisions_;
};
}  // namespace droppix
