#pragma once

#include <map>
#include <memory>

#include <sys/types.h>

using std::map;
using std::shared_ptr;

class Command;
class Env;
class Process;

class Tracer {
 public:
  /// Create a tracer linked to a specific rebuild environment
  Tracer(Env& env) : _env(env) {}

  /// Run a command in this tracer
  void run(shared_ptr<Command> cmd);

 private:
  /// Launch a command with tracing enabled
  void launchTraced(shared_ptr<Command> cmd);

  /// Called when we catch a system call in the traced process
  void handleSyscall(shared_ptr<Process> p);

  /// Called after a traced process issues a clone system call
  void handleClone(shared_ptr<Process> p, int flags);

  /// Called after a traced process issues a fork system call
  void handleFork(shared_ptr<Process> p);

  /// Called when a traced process exits
  void handleExit(shared_ptr<Process> p);

 private:
  /// The environment this tracer uses to resolve artifacts
  Env& _env;

  /// A map from process IDs to processes. Note that a process will appear multiple times if it uses
  /// multiple threads; all entries will point to the same Process instance.
  map<pid_t, shared_ptr<Process>> _processes;
};
