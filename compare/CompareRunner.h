#pragma once
#include "Types.h"

#include <atomic>
#include <deque>
#include <functional>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

namespace SmartMet::Spine { class TcpMultiQuery; }

#include <glibmm/dispatcher.h>

/**
 * Sends every QueryInfo to two target servers in parallel (via
 * TcpMultiQuery), compares the responses, and delivers CompareResult
 * objects back to the GTK main thread via Glib::Dispatcher.
 *
 * Usage:
 *   CompareRunner runner;
 *   runner.signal_result().connect([](CompareResult r){ ... });
 *   runner.signal_done().connect([](){ ... });
 *   runner.start(queries, server1_url, server2_url);
 */
class CompareRunner
{
 public:
  CompareRunner();
  ~CompareRunner();

  // Signals fired on the GTK main thread
  sigc::signal<void(CompareResult)>& signal_result() { return sig_result_; }
  sigc::signal<void()>& signal_done() { return sig_done_; }

  // Start processing.  Any previous run is stopped first.
  // max_concurrent limits how many queries are in flight simultaneously.
  void start(std::vector<QueryInfo> queries,
             std::string server1_url,
             std::string server2_url,
             int max_concurrent,
             size_t max_size);

  // Request early stop; blocks until the worker exits.
  void stop();

  // Request early stop without waiting for the worker.  Safe to call from
  // the GTK main thread.  Interrupts in-flight TcpMultiQuery requests via
  // their stop() method so they return immediately.
  void request_stop();

  bool is_running() const { return running_.load(); }

 private:
  void worker(std::vector<QueryInfo> queries,
              std::string server1_url,
              std::string server2_url,
              int max_concurrent,
              size_t max_size);

  void on_dispatch();

  // Worker thread
  std::thread thread_;
  std::atomic<bool> running_{false};
  std::atomic<bool> stop_requested_{false};

  // Thread-safe result queue
  Glib::Dispatcher dispatcher_;
  std::mutex mutex_;
  std::deque<CompareResult> result_queue_;
  bool done_pending_{false};

  // Live TcpMultiQuery objects that request_stop() should interrupt.
  std::mutex active_queries_mutex_;
  std::set<SmartMet::Spine::TcpMultiQuery*> active_queries_;

  // Signals emitted on the main thread
  sigc::signal<void(CompareResult)> sig_result_;
  sigc::signal<void()> sig_done_;
};
