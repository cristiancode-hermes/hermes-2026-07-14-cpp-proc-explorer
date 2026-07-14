#include <catch2/catch_test_macros.hpp>
#include <catch2/benchmark/catch_benchmark.hpp>
#include "proc_explorer/proc_explorer.hpp"

#include <print>
#include <fstream>

using namespace proc_explorer;

// ── Unit Tests: /proc readers ──

TEST_CASE("Read /proc/cpuinfo", "[proc][cpu]") {
  auto result = read_cpuinfo();
  REQUIRE(result.has_value());
  auto& cpu = result.value();
  std::println("CPU: {} ({}) - {} MHz, {} cores/{} siblings",
    cpu.model, cpu.vendor, cpu.mhz, cpu.cores, cpu.siblings);
  CHECK_FALSE(cpu.model.empty());
  CHECK_FALSE(cpu.vendor.empty());
  CHECK(cpu.mhz > 0);
  CHECK(cpu.cores >= 1);
  CHECK(cpu.siblings >= cpu.cores);
}

TEST_CASE("Read /proc/meminfo", "[proc][memory]") {
  auto result = read_meminfo();
  REQUIRE(result.has_value());
  auto& mem = result.value();
  std::println("Memory: {} KB total, {} KB available, {} KB free",
    mem.total_kb, mem.available_kb, mem.free_kb);
  CHECK(mem.total_kb > 0);
  CHECK(mem.available_kb > 0);
  CHECK(mem.free_kb > 0);
  CHECK(mem.total_kb >= mem.available_kb);
  CHECK(mem.total_kb >= mem.free_kb);
}

TEST_CASE("Read /proc/uptime", "[proc]") {
  auto result = read_uptime();
  REQUIRE(result.has_value());
  auto [uptime, idle] = result.value();
  std::println("Uptime: {} sec (idle: {} sec)", uptime, idle);
  CHECK(uptime > 0);
  CHECK(idle >= 0);  // idle can be less than uptime on single-core, more on multi-core
}

TEST_CASE("Read /proc/loadavg", "[proc]") {
  auto result = read_loadavg();
  REQUIRE(result.has_value());
  auto [l1, l5, l15, running, total] = result.value();
  std::println("Load: {:.2f} / {:.2f} / {:.2f} — {}/{} procs",
    l1, l5, l15, running, total);
  CHECK(l1 >= 0);
  CHECK(l5 >= 0);
  CHECK(l15 >= 0);
  CHECK(total >= 1);
  CHECK(running >= 0);
}

TEST_CASE("Read /proc/stat", "[proc]") {
  auto result = read_proc_stat();
  REQUIRE(result.has_value());
  auto& ps = result.value();
  std::println("Procs: total_forks={}, running={}, blocked={}",
    ps.total_forks, ps.procs_running, ps.procs_blocked);
  CHECK(ps.total_forks >= 0);
  CHECK(ps.procs_running >= 0);
  CHECK(ps.procs_blocked >= 0);
}

TEST_CASE("List PIDs", "[proc]") {
  auto result = list_pids();
  REQUIRE(result.has_value());
  auto& pids = result.value();
  std::println("Found {} processes", pids.size());
  CHECK(pids.size() > 5);  // At least a few processes running
  CHECK(pids[0] == 1);     // PID 1 should exist (init/systemd)
}

TEST_CASE("Read process 1 (init)", "[proc]") {
  auto result = read_process(1);
  REQUIRE(result.has_value());
  auto& p = result.value();
  std::println("PID 1: name={}, state={}, rss={}KB, threads={}",
    p.name, p.state, p.vm_rss_kb, p.num_threads);
  CHECK(p.pid == 1);
  CHECK_FALSE(p.name.empty());
  bool state_ok = (p.state == 'S' || p.state == 'R');
  CHECK(state_ok);
  CHECK(p.num_threads >= 1);
}

TEST_CASE("Read cmdline for PID 1", "[proc]") {
  auto result = read_cmdline(1);
  REQUIRE(result.has_value());
  std::println("PID 1 cmdline: {}", result.value());
  CHECK_FALSE(result.value().empty());
}

TEST_CASE("Read process environment (PID 1)", "[proc]") {
  auto result = read_environ(1);
  // environ may be restricted; just check it doesn't crash
  if (result) {
    auto& env = result.value();
    std::println("PID 1 env vars: {}", env.size());
  } else {
    std::println("PID 1 environ: {} (expected in containers)", result.error().message);
  }
  // Don't require — permission-dependent
}

TEST_CASE("Read memory maps (PID 1)", "[proc]") {
  auto result = read_maps(1);
  // maps may be restricted; just check it doesn't crash
  if (result) {
    auto& maps = result.value();
    std::println("PID 1 memory maps: {}", maps.size());
  } else {
    std::println("PID 1 maps: {} (expected in containers)", result.error().message);
  }
  // Don't require — permission-dependent
}

TEST_CASE("Read open file descriptors (PID 1)", "[proc]") {
  auto result = read_fds(1);
  // fds may be restricted; just check it doesn't crash
  if (result) {
    auto& fds = result.value();
    std::println("PID 1 open fds: {}", fds.size());
    CHECK(fds.size() >= 1);
  } else {
    std::println("PID 1 fds: {} (expected in containers)", result.error().message);
  }
  // Don't require — permission-dependent
}

TEST_CASE("Get all processes", "[proc][integration]") {
  auto result = get_all_processes();
  REQUIRE(result.has_value());
  auto& procs = result.value();
  std::println("Total processes: {}", procs.size());
  CHECK(procs.size() > 5);
  // Count running processes
  auto running_count = std::ranges::count_if(procs, [](const ProcessInfo& p) {
    return p.state == 'R';
  });
  std::println("Running processes: {}", running_count);
  CHECK(running_count >= 1);
}

TEST_CASE("Get system summary", "[proc][integration]") {
  auto result = get_system_summary();
  REQUIRE(result.has_value());
  auto& s = result.value();
  std::println("System Summary:");
  std::println("  CPU: {} ({} cores/{} threads, {:.0f} MHz)",
    s.cpu.model, s.cpu.cores, s.cpu.siblings, s.cpu.mhz);
  std::println("  Memory: {} KB total, {} KB available",
    s.mem.total_kb, s.mem.available_kb);
  std::println("  Uptime: {:.0f} sec, Load: {:.2f} {:.2f} {:.2f}",
    s.uptime_secs, s.load_1m, s.load_5m, s.load_15m);
  CHECK(s.total_procs > 0);
  CHECK(s.mem.total_kb > 0);
  CHECK(s.cpu.mhz > 0);
  CHECK(s.uptime_secs > 0);
}

TEST_CASE("Format size helper", "[util]") {
  CHECK(format_size(500) == "500 B");
  CHECK(format_size(2048) == "2.0 KB");
  CHECK(format_size(1048576) == "1.0 MB");
  CHECK(format_size(1073741824) == "1.0 GB");
}

// ── Benchmarks ──

TEST_CASE("Benchmark: list PIDs", "[benchmark]") {
  BENCHMARK("List PIDs") {
    return list_pids();
  };
}

TEST_CASE("Benchmark: read all processes", "[benchmark]") {
  BENCHMARK("Get all processes") {
    return get_all_processes();
  };
}

TEST_CASE("Benchmark: read memory info", "[benchmark]") {
  BENCHMARK("Read meminfo") {
    return read_meminfo();
  };
}
