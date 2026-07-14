#include "proc_explorer/proc_explorer.hpp"

#include <fstream>
#include <sstream>
#include <cstring>
#include <algorithm>
#include <ranges>
#include <charconv>
#include <cerrno>
#include <cctype>
#include <dirent.h>
#include <unistd.h>
#include <sys/resource.h>

namespace proc_explorer {

// ── Helper: read a /proc file (handles st_size == 0) ──
Result<std::string> read_proc_file(const fs::path& path) {
  std::ifstream file(path);
  if (!file.is_open()) {
    return std::unexpected(Error{
      errno, std::format("Cannot open {}: {}", path.c_str(), std::strerror(errno))
    });
  }
  std::ostringstream buf;
  buf << file.rdbuf();
  if (!buf) {
    return std::unexpected(Error{
      errno, std::format("Failed to read {}: {}", path.c_str(), std::strerror(errno))
    });
  }
  return buf.str();
}

// ── Helper: parse a key: value line from /proc files ──
static std::pair<std::string_view, std::string_view>
split_kv(std::string_view line) {
  auto pos = line.find(':');
  if (pos == std::string_view::npos) return {line, {}};
  auto key = line.substr(0, pos);
  // /proc uses "key\t: value" — trim trailing whitespace from key
  while (!key.empty() && (key.back() == ' ' || key.back() == '\t'))
    key.remove_suffix(1);
  auto value = line.substr(pos + 1);
  // Trim leading whitespace from value
  while (!value.empty() && (value.front() == ' ' || value.front() == '\t'))
    value.remove_prefix(1);
  return {key, value};
}

// ── Read /proc/stat ──
Result<ProcStat> read_proc_stat() {
  auto content = read_proc_file("/proc/stat");
  if (!content) return std::unexpected(content.error());

  ProcStat ps{};
  std::istringstream stream(content.value());
  std::string line;
  while (std::getline(stream, line)) {
    if (line.starts_with("processes ")) {
      std::from_chars(line.data() + 10, line.data() + line.size(), ps.total_forks);
    } else if (line.starts_with("procs_running ")) {
      std::from_chars(line.data() + 14, line.data() + line.size(), ps.procs_running);
    } else if (line.starts_with("procs_blocked ")) {
      std::from_chars(line.data() + 14, line.data() + line.size(), ps.procs_blocked);
    }
  }
  return ps;
}

// ── Read /proc/meminfo ──
Result<MemInfo> read_meminfo() {
  auto content = read_proc_file("/proc/meminfo");
  if (!content) return std::unexpected(content.error());

  MemInfo mem{};
  std::istringstream stream(content.value());
  std::string line;
  while (std::getline(stream, line)) {
    auto [key, value] = split_kv(line);
    // Remove "kB" suffix if present
    while (!value.empty() && (value.back() == 'k' || value.back() == 'B' || value.back() == ' '))
      value.remove_suffix(1);

    unsigned long val = 0;
    std::from_chars(value.data(), value.data() + value.size(), val);

    if (key == "MemTotal")       mem.total_kb = val;
    else if (key == "MemFree")   mem.free_kb = val;
    else if (key == "MemAvailable") mem.available_kb = val;
    else if (key == "Buffers")   mem.buffers_kb = val;
    else if (key == "Cached")    mem.cached_kb = val;
    else if (key == "SwapTotal") mem.swap_total_kb = val;
    else if (key == "SwapFree")  mem.swap_free_kb = val;
  }
  return mem;
}

// ── Read /proc/cpuinfo (info for first CPU) ──
Result<CpuInfo> read_cpuinfo() {
  auto content = read_proc_file("/proc/cpuinfo");
  if (!content) return std::unexpected(content.error());

  CpuInfo cpu{};
  std::istringstream stream(content.value());
  std::string line;
  while (std::getline(stream, line)) {
    auto [key, value] = split_kv(line);
    if (key == "processor") {
      std::from_chars(value.data(), value.data() + value.size(), cpu.processor);
    } else if (key == "vendor_id") {
      cpu.vendor = value;
    } else if (key == "model name") {
      cpu.model = value;
    } else if (key == "cpu MHz") {
      std::from_chars(value.data(), value.data() + value.size(), cpu.mhz);
    } else if (key == "cache size") {
      auto trimmed = value;
      while (!trimmed.empty() && (trimmed.back() == 'K' || trimmed.back() == 'B' || trimmed.back() == ' '))
        trimmed.remove_suffix(1);
      std::from_chars(trimmed.data(), trimmed.data() + trimmed.size(), cpu.cache_kb);
    } else if (key == "cpu cores") {
      std::from_chars(value.data(), value.data() + value.size(), cpu.cores);
    } else if (key == "siblings") {
      std::from_chars(value.data(), value.data() + value.size(), cpu.siblings);
    }
  }
  return cpu;
}

// ── Read /proc/uptime ──
Result<std::pair<double, double>> read_uptime() {
  auto content = read_proc_file("/proc/uptime");
  if (!content) return std::unexpected(content.error());
  double up = 0.0, idle = 0.0;
  std::istringstream(content.value()) >> up >> idle;
  return std::pair{up, idle};
}

// ── Read /proc/loadavg ──
Result<std::tuple<double, double, double, int, int>> read_loadavg() {
  auto content = read_proc_file("/proc/loadavg");
  if (!content) return std::unexpected(content.error());
  double l1=0, l5=0, l15=0;
  int running=0, total=0, last_pid=0;
  std::istringstream(content.value()) >> l1 >> l5 >> l15
    >> running; std::istringstream(content.value()) >> l1 >> l5 >> l15;
  // Re-parse for running/total and last_pid
  char slash;
  std::istringstream(content.value()) >> l1 >> l5 >> l15 >> running >> slash >> total >> last_pid;
  return std::tuple{l1, l5, l15, running, total};
}

// ── List PIDs ──
Result<std::vector<int>> list_pids() {
  std::vector<int> pids;
  DIR* dir = opendir("/proc");
  if (!dir) {
    return std::unexpected(Error{errno, std::format("Cannot open /proc: {}", std::strerror(errno))});
  }

  while (auto* entry = readdir(dir)) {
    if (entry->d_type == DT_DIR) {
      int pid = 0;
      auto [ptr, ec] = std::from_chars(entry->d_name,
        entry->d_name + std::strlen(entry->d_name), pid);
      if (ec == std::errc{} && pid > 0) {
        pids.push_back(pid);
      }
    }
  }
  closedir(dir);
  std::ranges::sort(pids);
  return pids;
}

// ── Read process status ──
Result<ProcessInfo> read_process(int pid) {
  auto path = std::format("/proc/{}", pid);
  auto status_path = fs::path(path) / "status";

  auto content = read_proc_file(status_path);
  if (!content) return std::unexpected(content.error());

  ProcessInfo proc;
  proc.pid = pid;

  std::istringstream stream(content.value());
  std::string line;
  while (std::getline(stream, line)) {
    auto [key, value] = split_kv(line);
    if (key == "Name") {
      proc.name = value;
    } else if (key == "State") {
      if (!value.empty()) proc.state = value.front();
    } else if (key == "VmRSS") {
      auto trimmed = value;
      while (!trimmed.empty() && (trimmed.back() == 'k' || trimmed.back() == 'B' || trimmed.back() == ' '))
        trimmed.remove_suffix(1);
      std::from_chars(trimmed.data(), trimmed.data() + trimmed.size(), proc.vm_rss_kb);
    } else if (key == "Threads") {
      std::from_chars(value.data(), value.data() + value.size(), proc.num_threads);
    } else if (key == "Nice") {
      std::from_chars(value.data(), value.data() + value.size(), proc.nice);
    }
  }

  // Read utime/stime from /proc/PID/stat
  auto stat_content = read_proc_file(fs::path(path) / "stat");
  if (stat_content) {
    // Parse fields from /proc/PID/stat
    // pid (comm) state ppid pgrp session tty_nr tpgid flags minflt cminflt majflt cmajflt
    // utime stime cutime cstime ...
    auto& s = stat_content.value();
    // Find closing paren of (comm)
    auto close_paren = s.rfind(')');
    if (close_paren != std::string::npos) {
      auto rest = std::string_view(s).substr(close_paren + 2);
      std::istringstream rest_ss{std::string(rest)};
      std::string state_str;
      int ppid = 0, pgrp = 0, session = 0;
      unsigned long utime = 0, stime = 0;
      rest_ss >> state_str >> ppid >> pgrp >> session;
      // Skip 4 more fields (tty_nr, tpgid, flags, minflt, cminflt, majflt, cmajflt)
      for (int i = 0; i < 7; ++i) { std::string tmp; rest_ss >> tmp; }
      rest_ss >> utime >> stime;
      proc.utime = utime;
      proc.stime = stime;
    }
  }

  // Read cmdline
  auto cmdline = read_cmdline(pid);
  if (cmdline) {
    proc.cmdline = std::move(cmdline.value());
  }

  return proc;
}

// ── Read /proc/PID/cmdline ──
Result<std::string> read_cmdline(int pid) {
  auto path = std::format("/proc/{}/cmdline", pid);
  auto content = read_proc_file(path);
  if (!content) return content;
  // Replace null bytes with spaces
  std::string result;
  for (char c : content.value()) {
    if (c == '\0') {
      if (!result.empty() && result.back() != ' ')
        result += ' ';
    } else {
      result += c;
    }
  }
  // Trim trailing space
  while (!result.empty() && result.back() == ' ')
    result.pop_back();
  return result;
}

// ── Read /proc/PID/maps ──
Result<std::vector<std::string>> read_maps(int pid) {
  auto content = read_proc_file(std::format("/proc/{}/maps", pid));
  if (!content) return std::unexpected(content.error());
  std::vector<std::string> lines;
  std::istringstream stream(content.value());
  std::string line;
  while (std::getline(stream, line)) {
    lines.push_back(std::move(line));
  }
  return lines;
}

// ── Read /proc/PID/environ ──
Result<std::vector<std::string>> read_environ(int pid) {
  auto content = read_proc_file(std::format("/proc/{}/environ", pid));
  if (!content) return std::unexpected(content.error());
  std::vector<std::string> vars;
  size_t start = 0;
  for (size_t i = 0; i < content.value().size(); ++i) {
    if (content.value()[i] == '\0') {
      if (i > start) {
        vars.push_back(content.value().substr(start, i - start));
      }
      start = i + 1;
    }
  }
  return vars;
}

// ── Read /proc/PID/fd ──
Result<std::vector<std::string>> read_fds(int pid) {
  auto fd_dir = std::format("/proc/{}/fd", pid);
  std::vector<std::string> fds;
  DIR* dir = opendir(fd_dir.c_str());
  if (!dir) {
    return std::unexpected(Error{errno, std::format("Cannot open {}: {}", fd_dir, std::strerror(errno))});
  }
  while (auto* entry = readdir(dir)) {
    if (entry->d_type == DT_LNK || entry->d_name[0] != '.') {
      std::string fd_name = entry->d_name;
      // Read link target
      std::string link_path = std::format("{}/{}", fd_dir, fd_name);
      char buf[256] = {};
      auto len = readlink(link_path.c_str(), buf, sizeof(buf) - 1);
      if (len > 0) {
        buf[len] = '\0';
        fds.push_back(std::format("{} -> {}", fd_name, buf));
      } else {
        fds.push_back(std::move(fd_name));
      }
    }
  }
  closedir(dir);
  std::ranges::sort(fds);
  return fds;
}

// ── Get all processes ──
Result<std::vector<ProcessInfo>> get_all_processes() {
  auto pids = list_pids();
  if (!pids) return std::unexpected(pids.error());

  std::vector<ProcessInfo> processes;
  for (int pid : pids.value()) {
    auto proc = read_process(pid);
    if (proc) {
      processes.push_back(std::move(proc.value()));
    }
  }
  return processes;
}

// ── Get system summary ──
Result<SysSummary> get_system_summary() {
  SysSummary summary{};

  auto mem = read_meminfo();
  if (mem) summary.mem = mem.value();

  auto cpu = read_cpuinfo();
  if (cpu) summary.cpu = cpu.value();

  auto uptime = read_uptime();
  if (uptime) {
    summary.uptime_secs = uptime.value().first;
  }

  auto loadavg = read_loadavg();
  if (loadavg) {
    std::tie(summary.load_1m, summary.load_5m, summary.load_15m,
      summary.running_procs, summary.total_procs) = loadavg.value();
  }

  auto stat = read_proc_stat();
  if (stat) {
    summary.running_procs = stat.value().procs_running;
  }

  return summary;
}

// ── Format size ──
std::string format_size(uint64_t bytes) {
  if (bytes >= 1073741824) return std::format("{:.1f} GB", bytes / 1073741824.0);
  if (bytes >= 1048576)    return std::format("{:.1f} MB", bytes / 1048576.0);
  if (bytes >= 1024)       return std::format("{:.1f} KB", bytes / 1024.0);
  return std::format("{} B", bytes);
}

} // namespace proc_explorer
