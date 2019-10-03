#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <forward_list>
#include <iostream>
#include <limits>
#include <list>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include <fcntl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <capnp/list.h>
#include <capnp/message.h>
#include <capnp/serialize.h>

#include "core/BuildGraph.hh"
#include "core/Command.hh"
#include "core/dodorun.hh"
#include "db/Serializer.hh"
#include "db/db.capnp.h"
#include "tracing/ptrace.hh"
#include "tracing/Tracer.hh"
#include "ui/log.hh"
#include "ui/options.hh"
#include "ui/util.hh"

using std::forward_list;
using std::string;

// Declare the global command-line options struct
dodo_options options;

/**
 * Parse command line options and return a dodo_options struct.
 */
void parse_argv(forward_list<string> argv) {
  // Loop until we've consumed all command line arguments
  while (!argv.empty()) {
    // Take the first argument off the list
    string arg = argv.front();
    argv.pop_front();

    if (arg == "--debug") {
      options.log_source_locations = true;
      options.log_threshold = LogLevel::Info;

    } else if (arg == "--no-color") {
      options.color_output = false;

    } else if (arg == "-v") {
      options.log_threshold = LogLevel::Warning;

    } else if (arg == "-vv") {
      options.log_threshold = LogLevel::Info;

    } else if (arg == "-vvv") {
      options.log_threshold = LogLevel::Verbose;

    } else if (arg == "--fingerprint") {
      if (!argv.empty()) {
        if (argv.front() == "none") {
          options.fingerprint = FingerprintLevel::None;
        } else if (argv.front() == "local") {
          options.fingerprint = FingerprintLevel::Local;
        } else if (argv.front() == "all") {
          options.fingerprint = FingerprintLevel::All;
        } else {
          FAIL << "Please specifiy a fingerprint level: none, local, or all.";
        }

        argv.pop_front();
      }

    } else if (arg == "--changed") {
      if (!argv.empty()) {
        options.explicitly_changed.insert(argv.front());
        argv.pop_front();
      } else {
        std::cerr << "Please specify a file to mark as changed.\n";
        exit(1);
      }

    } else if (arg == "--unchanged") {
      if (!argv.empty()) {
        options.explicitly_unchanged.insert(argv.front());
        argv.pop_front();
      } else {
        std::cerr << "Please specify a file to mark as unchanged.\n";
        exit(1);
      }

    } else if (arg == "--dry-run") {
      options.dry_run = true;

    } else if (arg == "-j") {
      if (!argv.empty()) {
        long specified_jobs = std::stol(argv.front());
        argv.pop_front();

        if (specified_jobs < 1) {
          std::cerr << "Invalid number of jobs: specify at least one.\n";
          exit(1);
        }
        options.parallel_jobs = specified_jobs;
      } else {
        std::cerr << "Please specify a number of jobs to use" << std::endl;
        exit(1);
      }

    } else if (arg == "--visualize") {
      options.visualize = true;

    } else if (arg == "--visualize-all") {
      options.visualize = true;
      options.show_sysfiles = true;

    } else if (arg == "--hide-collapsed") {
      options.show_collapsed = false;

    } else {
      std::cerr << "Invalid argument " << arg << std::endl;
      exit(1);
    }
  }
}

static bool stderr_supports_colors() {
  return isatty(STDERR_FILENO) && getenv("TERM") != nullptr;
}

/**
 * This is the entry point for the dodo command line tool
 */
int main(int argc, char* argv[]) {
  // Set color output based on TERM setting (can be overridden with command line option)
  if (!stderr_supports_colors()) options.color_output = false;

  // Parse command line options
  parse_argv(forward_list<string>(argv + 1, argv + argc));

  // Get the current working directory
  char* cwd = getcwd(nullptr, 0);
  FAIL_IF(cwd == nullptr) << "Failed to get current working directory: " << ERR;

  // Initialize build graph and a tracer instance
  BuildGraph graph(cwd);
  Tracer tracer(graph);

  // Clean up after getcwd
  free(cwd);

  // Open the database
  int db_fd = open("db.dodo", O_RDWR);

  // If the database doesn't exist, run a default build
  if (db_fd == -1) {
    std::shared_ptr<Command> root(new Command("Dodofile", {"Dodofile"}));
    graph.setRootCommand(root);
  
    graph.run(tracer);

    Serializer serializer("db.dodo");
    graph.serialize(serializer);
    
    return 0;
    
  } else {
    // Although the documentation recommends against this, we implicitly trust the
    // database anyway. Without this we may hit the recursion limit.
    ::capnp::ReaderOptions capnp_options;
    capnp_options.traversalLimitInWords = std::numeric_limits<uint64_t>::max();
    
    ::capnp::StreamFdMessageReader message(db_fd, capnp_options);
    auto old_graph = message.getRoot<db::Graph>();
    auto old_files = old_graph.getFiles();
    auto old_commands = old_graph.getCommands();

    // For now, fingerprint any time we have fingerprinting enabled on the tracing end
    bool use_fingerprints = options.fingerprint == FingerprintLevel::Local ||
                            options.fingerprint == FingerprintLevel::All;

    RebuildState rebuild_state(old_graph, use_fingerprints, options.explicitly_changed,
                               options.explicitly_unchanged);

    pid_t dry_run_pid = 1;
    std::map<pid_t, old_command*> wait_worklist;
    while (true) {
      auto run_command = rebuild_state.rebuild(use_fingerprints, options.dry_run,
                                               wait_worklist.size(), options.parallel_jobs);
      if (run_command == nullptr) {
        if (wait_worklist.empty()) {
          // We're done!
          break;
        } else {
          pid_t child;
          if (options.dry_run) {
            child = wait_worklist.begin()->first;
          } else {
            int wait_status;
            child = wait(&wait_status);
            FAIL_IF(child == -1) << "Error waiting for child: " << ERR;

            trace_step(tracer, child, wait_status);
            if (!WIFEXITED(wait_status) && !WIFSIGNALED(wait_status)) {
              continue;
            }
          }

          auto child_entry = wait_worklist.find(child);
          if (child_entry != wait_worklist.end()) {
            old_command* child_command = child_entry->second;
            wait_worklist.erase(child_entry);

            rebuild_state.mark_complete(use_fingerprints, options.dry_run, child_command);
          }
          continue;
        }
      }

      // Print that we will run it
      write_shell_escaped(std::cout, run_command->executable);
      for (auto arg : run_command->args) {
        std::cout << " ";
        write_shell_escaped(std::cout, arg);
      }

      // Print redirections
      for (auto initial_fd_entry : old_commands[run_command->id].getInitialFDs()) {
        std::cout << " ";
        if (!(initial_fd_entry.getFd() == fileno(stdin) && initial_fd_entry.getCanRead() &&
              !initial_fd_entry.getCanWrite()) &&
            !(initial_fd_entry.getFd() == fileno(stdout) && !initial_fd_entry.getCanRead() &&
              initial_fd_entry.getCanWrite())) {
          std::cout << initial_fd_entry.getFd();
        }
        if (initial_fd_entry.getCanRead()) {
          std::cout << '<';
        }
        if (initial_fd_entry.getCanWrite()) {
          std::cout << '>';
        }
        if (rebuild_state.files[initial_fd_entry.getFileID()]->is_pipe) {
          std::cout << "/proc/dodo/pipes/" << initial_fd_entry.getFileID();
        } else {
          write_shell_escaped(std::cout, rebuild_state.files[initial_fd_entry.getFileID()]->path);
        }
      }
      std::cout << std::endl;

      // Run it!
      pid_t child_pid;
      if (options.dry_run) {
        child_pid = dry_run_pid;
        dry_run_pid++;
      } else {
        // Set up initial fds
        std::vector<InitialFdEntry> file_actions;
        std::vector<int> opened_fds;
        int max_fd = 0;
        for (auto initial_fd_entry : old_commands[run_command->id].getInitialFDs()) {
          int fd = initial_fd_entry.getFd();
          if (fd > max_fd) {
            max_fd = fd;
          }
        }
        for (auto initial_fd_entry : old_commands[run_command->id].getInitialFDs()) {
          auto file = rebuild_state.files[initial_fd_entry.getFileID()];
          int open_fd_storage;
          int* open_fd_ref;
          if (file->is_pipe) {
            if (file->scheduled_for_creation) {
              file->scheduled_for_creation = false;
              int pipe_fds[2];
              // FIXME(portability): pipe2 is Linux-specific; fall back to pipe+fcntl?
              FAIL_IF(pipe2(pipe_fds, O_CLOEXEC)) << "Failed to create pipe: " << ERR;

              file->pipe_reader_fd = pipe_fds[0];
              file->pipe_writer_fd = pipe_fds[1];
            }

            if (initial_fd_entry.getCanRead()) {
              open_fd_ref = &file->pipe_reader_fd;
            } else {  // TODO: check for invalid read/write combinations?
              open_fd_ref = &file->pipe_writer_fd;
            }
          } else {
            int flags = O_CLOEXEC;
            if (initial_fd_entry.getCanRead() && initial_fd_entry.getCanWrite()) {
              flags |= O_RDWR;
            } else if (initial_fd_entry.getCanWrite()) {
              flags |= O_WRONLY;
            } else {  // TODO: what if the database has no permissions for some
                      // reason?
              flags |= O_RDONLY;
            }
            if (file->scheduled_for_creation) {
              file->scheduled_for_creation = false;
              flags |= O_CREAT | O_TRUNC;
            }
            mode_t mode = old_files[initial_fd_entry.getFileID()].getMode();
            open_fd_storage = open(file->path.c_str(), flags, mode);
            FAIL_IF(open_fd_storage == -1) << "Failed to open output: " << ERR;
            open_fd_ref = &open_fd_storage;
          }
          // Ensure that the dup2s won't step on each other's toes
          if (*open_fd_ref <= max_fd) {
            int new_fd = fcntl(*open_fd_ref, F_DUPFD_CLOEXEC, max_fd + 1);
            FAIL_IF(new_fd == -1) << "Failed to remap fd: " << ERR;
            close(*open_fd_ref);
            *open_fd_ref = new_fd;
          }
          if (!file->is_pipe) {
            opened_fds.push_back(*open_fd_ref);
          }
          file_actions.push_back({ *open_fd_ref, initial_fd_entry.getFd() });
        }
        // Spawn the child
        std::shared_ptr<Command> middle_cmd(
            new Command(run_command->executable, run_command->args));
        child_pid = start_command(middle_cmd, file_actions);
        tracer.newProcess(child_pid, middle_cmd);
        
        // Free what we can
        for (auto open_fd : opened_fds) {
          close(open_fd);
        }
        for (auto initial_fd_entry : old_commands[run_command->id].getInitialFDs()) {
          auto file = rebuild_state.files[initial_fd_entry.getFileID()];
          if (file->is_pipe) {
            if (initial_fd_entry.getCanRead()) {
              file->pipe_reader_references -= 1;
              if (file->pipe_reader_references == 0) {
                close(file->pipe_reader_fd);
              }
            } else {
              file->pipe_writer_references -= 1;
              if (file->pipe_writer_references == 0) {
                close(file->pipe_writer_fd);
              }
            }
          }
        }
      }

      wait_worklist[child_pid] = run_command;
    }
    if (options.visualize) {
      rebuild_state.visualize(options.show_sysfiles, options.show_collapsed);
    }
    return 0;
  }
}
