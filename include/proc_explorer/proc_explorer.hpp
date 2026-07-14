#pragma once

#include <expected>
#include <string>
#include <vector>
#include <string_view>
#include <filesystem>
#include <unordered_map>
#include <cstdint>
#include <print>
#include <format>

namespace proc_explorer {

namespace fs = std::filesystem;

// ── Error handling ──
struct Error {
  int code;
  std::string message;
};

template <typename T>
using Result = std::expected<T, Error>;

// ── Process info ──
struct ProcessInfo {
  int pid{};
  std::string name;
  char state{};         // R, S, D, Z, T, X
  uint64_t vm_rss_kb{};
  unsigned long utime{};
  unsigned long stime{};
  long nice{};
  long num_threads{};
  std::string cmdline;
};

// ── Memory info ──
struct MemInfo {
  uint64_t total_kb{};
  uint64_t free_kb{};
  uint64_t available_kb{};
  uint64_t buffers_kb{};
  uint64_t cached_kb{};
  uint64_t swap_total_kb{};
  uint64_t swap_free_kb{};
};

// ── CPU info ──
struct CpuInfo {
  unsigned processor{};
  std::string vendor;
  std::string model;
  double mhz{};
  unsigned cache_kb{};
  unsigned cores{};
  unsigned siblings{};
};

// ── System summary ──
struct SysSummary {
  MemInfo mem;
  CpuInfo cpu;
  double uptime_secs{};
  double load_1m{}, load_5m{}, load_15m{};
  int total_procs{};
  int running_procs{};
};

// ── /proc Readers ──

/// Read /proc/stat and extract process counts
struct ProcStat {
  long total_forks{};
  int procs_running{};
  int procs_blocked{};
};
Result<ProcStat> read_proc_stat();

/// Read /proc/meminfo
Result<MemInfo> read_meminfo();

/// Read /proc/cpuinfo — returns info for first CPU
Result<CpuInfo> read_cpuinfo();

/// Read /proc/uptime
Result<std::pair<double, double>> read_uptime(); // (uptime_secs, idle_secs)

/// Read /proc/loadavg
Result<std::tuple<double, double, double, int, int>> read_loadavg(); // (1m, 5m, 15m, running/total, last_pid)

/// List all process directories in /proc
Result<std::vector<int>> list_pids();

/// Read /proc/PID/status
Result<ProcessInfo> read_process(int pid);

/// Read /proc/PID/cmdline
Result<std::string> read_cmdline(int pid);

/// Read /proc/PID/maps — list memory mapped regions
Result<std::vector<std::string>> read_maps(int pid);

/// Read /proc/PID/environ
Result<std::vector<std::string>> read_environ(int pid);

/// Read /proc/PID/fd — list open file descriptors
Result<std::vector<std::string>> read_fds(int pid);

/// Read a whole /proc file into string (handles st_size == 0)
Result<std::string> read_proc_file(const fs::path& path);

/// Get all processes with filtering
Result<std::vector<ProcessInfo>> get_all_processes();

/// Get system summary
Result<SysSummary> get_system_summary();

/// Format size in human-readable form
std::string format_size(uint64_t bytes);

} // namespace proc_explorer
