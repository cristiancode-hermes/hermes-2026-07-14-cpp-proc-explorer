#include "proc_explorer/proc_explorer.hpp"

#include <CLI/CLI.hpp>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include <iostream>
#include <algorithm>
#include <ranges>
#include <print>
#include <thread>
#include <chrono>
#include <sstream>

using namespace proc_explorer;
using json = nlohmann::json;
namespace chrono = std::chrono;
using namespace std::chrono_literals;

// ── ANSI helpers ──
namespace ansi {
  constexpr auto reset    = "\033[0m";
  constexpr auto bold     = "\033[1m";
  constexpr auto red      = "\033[31m";
  constexpr auto green    = "\033[32m";
  constexpr auto yellow   = "\033[33m";
  constexpr auto blue     = "\033[34m";
  constexpr auto magenta  = "\033[35m";
  constexpr auto cyan     = "\033[36m";
  constexpr auto white    = "\033[37m";
  constexpr auto gray     = "\033[90m";
}

// ── Helpers ──
static std::string format_uptime(double secs) {
  auto days = static_cast<int>(secs / 86400.0);
  auto hours = static_cast<int>((secs - days * 86400.0) / 3600.0);
  auto mins = static_cast<int>((secs - days * 86400.0 - hours * 3600.0) / 60.0);
  if (days > 0) return std::format("{}d {:02d}h {:02d}m", days, hours, mins);
  if (hours > 0) return std::format("{:02d}h {:02d}m", hours, mins);
  return std::format("{:02d}m", mins);
}

static std::string_view state_str(char s) {
  switch (s) {
    case 'R': return "Running";
    case 'S': return "Sleeping";
    case 'D': return "DiskSleep";
    case 'Z': return "Zombie";
    case 'T': return "Stopped";
    case 'X': return "Dead";
    default:  return "Unknown";
  }
}

static std::string_view state_color(char s) {
  switch (s) {
    case 'R': return ansi::green;
    case 'S': return ansi::cyan;
    case 'D': return ansi::yellow;
    case 'Z': return ansi::red;
    case 'T': return ansi::magenta;
    default:  return ansi::gray;
  }
}

static std::string fmt_rss(uint64_t kb) {
  if (kb == 0) return "0";
  return std::format("{}M", kb / 1024);
}

static std::string sep_line(char c = '-', int n = 65) {
  return std::string(n, c);
}

// ── Command: list processes ──
static int cmd_list(const std::string& filter_name, const std::string& filter_state,
                    bool sort_mem, bool no_color, bool json_output, int limit) {
  auto procs = get_all_processes();
  if (!procs) {
    spdlog::error("Failed to list processes: {}", procs.error().message);
    return 1;
  }

  auto& vec = procs.value();
  auto filtered = vec | std::views::filter([&](const ProcessInfo& p) {
    if (!filter_name.empty() && p.name.find(filter_name) == std::string::npos)
      return false;
    if (!filter_state.empty()) {
      auto s = state_str(p.state);
      return s.starts_with(filter_state) || p.state == filter_state.front();
    }
    return true;
  }) | std::ranges::to<std::vector>();

  if (sort_mem) {
    std::ranges::sort(filtered, [](const auto& a, const auto& b) {
      return a.vm_rss_kb > b.vm_rss_kb;
    });
  } else {
    std::ranges::sort(filtered, [](const auto& a, const auto& b) {
      return a.pid < b.pid;
    });
  }

  if (limit > 0 && static_cast<int>(filtered.size()) > limit)
    filtered.resize(limit);

  if (json_output) {
    json arr = json::array();
    for (const auto& p : filtered) {
      arr.push_back({
        {"pid", p.pid}, {"name", p.name}, {"state", std::string(1, p.state)},
        {"state_label", state_str(p.state)}, {"vm_rss_kb", p.vm_rss_kb},
        {"threads", p.num_threads}, {"cmdline", p.cmdline},
        {"utime", p.utime}, {"stime", p.stime}, {"nice", p.nice}
      });
    }
    std::println("{}", arr.dump(2));
  } else {
    if (!no_color) {
      std::cout << ansi::bold << ansi::white
                << std::format("{:>6} {:<20} {:>8} {:>8} {:>5} {:>5} {}",
                              "PID", "NAME", "STATE", "RSS", "THR", "NICE", "CMDLINE")
                << ansi::reset << '\n';
    } else {
      std::cout << std::format("{:>6} {:<20} {:>8} {:>8} {:>5} {:>5} {}",
                              "PID", "NAME", "STATE", "RSS", "THR", "NICE", "CMDLINE")
                << '\n';
    }
    std::cout << sep_line('-', 95) << '\n';

    for (const auto& p : filtered) {
      if (!no_color) {
        std::cout << ansi::white << ansi::bold << std::format("{:>6}", p.pid) << ansi::reset
                  << ' ' << state_color(p.state) << std::format("{:<20}", p.name.substr(0, 20)) << ansi::reset
                  << ' ' << state_color(p.state) << std::format("{:>8}", state_str(p.state).substr(0, 8)) << ansi::reset
                  << ' ' << (p.vm_rss_kb >= 100000 ? ansi::yellow : (p.vm_rss_kb > 0 ? ansi::green : ansi::gray))
                  << std::format("{:>8}", fmt_rss(p.vm_rss_kb)) << ansi::reset
                  << std::format(" {:>5} {:>5}", p.num_threads, p.nice)
                  << ' ' << p.cmdline.substr(0, 40) << '\n';
      } else {
        std::cout << std::format("{:>6} {:<20} {:>8} {:>8} {:>5} {:>5} {}",
                                p.pid, p.name.substr(0, 20),
                                state_str(p.state).substr(0, 8),
                                fmt_rss(p.vm_rss_kb),
                                p.num_threads, p.nice,
                                p.cmdline.substr(0, 40)) << '\n';
      }
    }
  }
  return 0;
}

// ── Command: system summary ──
static int cmd_summary(bool json_output) {
  auto summary = get_system_summary();
  if (!summary) {
    spdlog::error("Failed to get system summary: {}", summary.error().message);
    return 1;
  }

  auto& s = summary.value();

  if (json_output) {
    json j = {
      {"uptime_secs", s.uptime_secs},
      {"load", {s.load_1m, s.load_5m, s.load_15m}},
      {"processes", {{"total", s.total_procs}, {"running", s.running_procs}}},
      {"cpu", {{"model", s.cpu.model}, {"vendor", s.cpu.vendor},
               {"mhz", s.cpu.mhz}, {"cores", s.cpu.cores}, {"siblings", s.cpu.siblings}}},
      {"memory", {{"total_kb", s.mem.total_kb}, {"free_kb", s.mem.free_kb},
                   {"available_kb", s.mem.available_kb},
                   {"used_kb", s.mem.total_kb - s.mem.free_kb},
                   {"swap_total_kb", s.mem.swap_total_kb},
                   {"swap_free_kb", s.mem.swap_free_kb}}}
    };
    std::println("{}", j.dump(2));
  } else {
    std::cout << ansi::bold << "System Summary" << ansi::reset << '\n';
    std::cout << ansi::cyan << sep_line('=', 30) << ansi::reset << '\n';
    std::cout << std::format("Uptime:  {}", format_uptime(s.uptime_secs)) << '\n';
    std::cout << std::format("Load:    {:.2f} / {:.2f} / {:.2f}", s.load_1m, s.load_5m, s.load_15m) << '\n';
    std::cout << std::format("Procs:   {} total, {} running", s.total_procs, s.running_procs) << '\n';

    std::cout << '\n' << ansi::bold << "CPU" << ansi::reset << '\n';
    std::cout << ansi::cyan << sep_line('=', 20) << ansi::reset << '\n';
    std::cout << std::format("Model:   {}", s.cpu.model) << '\n';
    std::cout << std::format("Vendor:  {}", s.cpu.vendor) << '\n';
    std::cout << std::format("MHz:     {:.0f}", s.cpu.mhz) << '\n';
    std::cout << std::format("Cores:   {} physical / {} logical", s.cpu.cores, s.cpu.siblings) << '\n';
    std::cout << std::format("Cache:   {} KB", s.cpu.cache_kb) << '\n';

    auto used_kb = s.mem.total_kb - s.mem.free_kb;
    double pct = s.mem.total_kb > 0 ? (100.0 * used_kb / s.mem.total_kb) : 0.0;

    std::cout << '\n' << ansi::bold << "Memory" << ansi::reset << '\n';
    std::cout << ansi::cyan << sep_line('=', 20) << ansi::reset << '\n';
    std::cout << std::format("Total:     {}", proc_explorer::format_size(s.mem.total_kb * 1024)) << '\n';
    std::cout << std::format("Used:      {} ({:.1f}%)", proc_explorer::format_size(used_kb * 1024), pct) << '\n';
    std::cout << std::format("Free:      {}", proc_explorer::format_size(s.mem.free_kb * 1024)) << '\n';
    std::cout << std::format("Available: {}", proc_explorer::format_size(s.mem.available_kb * 1024)) << '\n';
    if (s.mem.swap_total_kb > 0) {
      std::cout << std::format("Swap:      {} total, {} free",
        proc_explorer::format_size(s.mem.swap_total_kb * 1024),
        proc_explorer::format_size(s.mem.swap_free_kb * 1024)) << '\n';
    }
  }
  return 0;
}

// ── Command: process detail ──
static int cmd_detail(int pid, bool /*no_color*/, bool json_output) {
  auto proc = read_process(pid);
  if (!proc) {
    spdlog::error("Process {}: {}", pid, proc.error().message);
    return 1;
  }

  auto& p = proc.value();

  if (json_output) {
    json j = {
      {"pid", p.pid}, {"name", p.name}, {"state", std::string(1, p.state)},
      {"state_label", state_str(p.state)}, {"vm_rss_kb", p.vm_rss_kb},
      {"threads", p.num_threads}, {"cmdline", p.cmdline},
      {"utime", p.utime}, {"stime", p.stime}, {"nice", p.nice}
    };
    auto maps = read_maps(pid);
    if (maps) j["maps_count"] = maps.value().size();
    auto env = read_environ(pid);
    if (env) j["environ_count"] = env.value().size();
    auto fds = read_fds(pid);
    if (fds) j["open_fds_count"] = fds.value().size();
    std::println("{}", j.dump(2));
  } else {
    std::cout << ansi::bold << ansi::white
              << std::format("Process #{} -- {}", pid, p.name)
              << ansi::reset << '\n';
    std::cout << ansi::cyan << sep_line('=', 30) << ansi::reset << '\n';
    std::cout << std::format("PID:      {}", p.pid) << '\n';
    std::cout << std::format("Name:     {}", p.name) << '\n';
    std::cout << "State:    " << state_color(p.state) << state_str(p.state)
              << ansi::reset << " (" << p.state << ')' << '\n';
    std::cout << std::format("RSS:      {} KB ({} MB)", p.vm_rss_kb, p.vm_rss_kb / 1024) << '\n';
    std::cout << std::format("Threads:  {}", p.num_threads) << '\n';
    std::cout << std::format("Nice:     {}", p.nice) << '\n';
    std::cout << std::format("Utime:    {} ticks", p.utime) << '\n';
    std::cout << std::format("Stime:    {} ticks", p.stime) << '\n';
    std::cout << "Cmdline:  " << (p.cmdline.empty() ? "(kernel/thread)" : p.cmdline) << '\n';

    auto maps = read_maps(pid);
    if (maps && !maps.value().empty()) {
      std::cout << '\n' << ansi::bold << "Memory Maps (first 10)" << ansi::reset << '\n';
      int count = 0;
      for (const auto& line : maps.value()) {
        if (++count > 10) { std::cout << "  ... and " << (maps.value().size() - 10) << " more\n"; break; }
        std::cout << "  " << line.substr(0, 80) << '\n';
      }
    }

    auto fds = read_fds(pid);
    if (fds && !fds.value().empty()) {
      std::cout << '\n' << ansi::bold << "Open File Descriptors (first 10)" << ansi::reset << '\n';
      int count = 0;
      for (const auto& fd : fds.value()) {
        if (++count > 10) { std::cout << "  ... and " << (fds.value().size() - 10) << " more\n"; break; }
        std::cout << "  " << fd << '\n';
      }
    }

    auto env = read_environ(pid);
    if (env && !env.value().empty()) {
      std::cout << '\n' << ansi::bold << "Environment (first 10)" << ansi::reset << '\n';
      int count = 0;
      for (const auto& var : env.value()) {
        if (++count > 10) { std::cout << "  ... and " << (env.value().size() - 10) << " more\n"; break; }
        auto eq = var.find('=');
        if (eq != std::string::npos) {
          std::cout << "  " << var.substr(0, eq) << '=' << var.substr(eq + 1).substr(0, 60) << '\n';
        } else {
          std::cout << "  " << var << '\n';
        }
      }
    }
  }
  return 0;
}

// ── Command: monitor ──
static int cmd_monitor(const std::string& filter_name, bool sort_mem,
                       bool no_color, int interval_secs, int limit) {
  auto interval = std::chrono::seconds(interval_secs);
  while (true) {
    std::cout << "\033[2J\033[H";

    auto summary = get_system_summary();
    if (summary) {
      auto& s = summary.value();
      auto used_kb = s.mem.total_kb - s.mem.free_kb;
      double pct = s.mem.total_kb > 0 ? (100.0 * used_kb / s.mem.total_kb) : 0.0;
      std::cout << ansi::bold << "proc-explorer" << ansi::reset
                << std::format(" -- {} | Load: {:.2f} {:.2f} {:.2f} | Procs: {} running / {} total | Up: {}",
                              format_uptime(s.uptime_secs),
                              s.load_1m, s.load_5m, s.load_15m,
                              s.running_procs, s.total_procs,
                              format_uptime(s.uptime_secs))
                << '\n';
      std::cout << std::format("Memory: {} / {} ({:.1f}%)",
                              proc_explorer::format_size(used_kb * 1024),
                              proc_explorer::format_size(s.mem.total_kb * 1024), pct) << '\n';
    }

    auto procs = get_all_processes();
    if (!procs) {
      std::this_thread::sleep_for(interval);
      continue;
    }

    auto filtered = procs.value()
      | std::views::filter([&](const ProcessInfo& p) {
          return filter_name.empty() || p.name.find(filter_name) != std::string::npos;
        })
      | std::ranges::to<std::vector>();

    if (sort_mem) {
      std::ranges::sort(filtered, [](const auto& a, const auto& b) { return a.vm_rss_kb > b.vm_rss_kb; });
    } else {
      std::ranges::sort(filtered, [](const auto& a, const auto& b) { return a.pid < b.pid; });
    }

    if (limit > 0 && static_cast<int>(filtered.size()) > limit)
      filtered.resize(limit);

    std::cout << std::format("{:>6} {:<20} {:>8} {:>8} {:>5} {:>5} {}\n",
                            "PID", "NAME", "STATE", "RSS", "THR", "NICE", "CMDLINE");
    std::cout << sep_line('-', 95) << '\n';

    for (const auto& p : filtered) {
      std::cout << std::format("{:>6} {:<20} {:>8} {:>8} {:>5} {:>5} {}\n",
                              p.pid, p.name.substr(0, 20),
                              state_str(p.state).substr(0, 8),
                              fmt_rss(p.vm_rss_kb),
                              p.num_threads, p.nice,
                              p.cmdline.substr(0, 40));
    }

    std::this_thread::sleep_for(interval);
  }
  return 0;
}

// ── Main ──
int main(int argc, char** argv) {
  CLI::App app{"proc-explorer -- /proc filesystem explorer in C++23"};

  bool no_color = false;
  bool json_output = false;
  std::string filter_name, filter_state;
  bool sort_mem = false;
  int limit = 0;
  int detail_pid = 0;
  int mon_interval = 2;

  // Global flags
  app.add_flag("--no-color", no_color, "Disable ANSI colors");
  app.add_flag("--json,-j", json_output, "JSON output mode");

  // list
  auto* list_cmd = app.add_subcommand("list", "List all processes");
  list_cmd->add_option("-n,--name", filter_name, "Filter by process name (substring)");
  list_cmd->add_option("-s,--state", filter_state, "Filter by state (R=Running, S=Sleeping, D=Disk Sleep, Z=Zombie)");
  list_cmd->add_flag("--sort-mem,-m", sort_mem, "Sort by memory usage (descending)");
  list_cmd->add_option("-l,--limit", limit, "Limit number of results");

  // summary
  auto* summary_cmd = app.add_subcommand("summary", "Show system summary");

  // detail
  auto* detail_cmd = app.add_subcommand("detail", "Show process details");
  detail_cmd->add_option("pid", detail_pid, "Process ID")->required();

  // monitor
  auto* monitor_cmd = app.add_subcommand("monitor", "Real-time process monitor");
  monitor_cmd->add_option("--interval,-i", mon_interval, "Refresh interval in seconds (default: 2)");
  monitor_cmd->add_option("-n,--name", filter_name, "Filter by process name");
  monitor_cmd->add_flag("--sort-mem,-m", sort_mem, "Sort by memory usage");
  monitor_cmd->add_option("-l,--limit", limit, "Limit results");

  app.require_subcommand(0, 1);

  try { CLI11_PARSE(app, argc, argv); }
  catch (const CLI::ParseError& e) { return app.exit(e); }

  if (*summary_cmd)       return cmd_summary(json_output);
  if (*detail_cmd)        return cmd_detail(detail_pid, no_color, json_output);
  if (*monitor_cmd)       return cmd_monitor(filter_name, sort_mem, no_color, mon_interval, limit);
  /* default: list */     return cmd_list(filter_name, filter_state, sort_mem, no_color, json_output, limit);
}
