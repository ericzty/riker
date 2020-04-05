#pragma once

#include <memory>
#include <ostream>
#include <string>
#include <vector>

#include "core/Artifact.hh"
#include "core/IR.hh"
#include "ui/options.hh"

class Command;
class Graphviz;
class Tracer;

using std::make_shared;
using std::string;
using std::vector;

class BuildGraph {
 public:
  /****** Constructors ******/
  BuildGraph() {}

  BuildGraph(string executable, vector<string> arguments);

  // Disallow Copy
  BuildGraph(const BuildGraph&) = delete;
  BuildGraph& operator=(const BuildGraph&) = delete;

  // Allow Move
  BuildGraph(BuildGraph&&) = default;
  BuildGraph& operator=(BuildGraph&&) = default;

  /****** Non-trivial methods ******/

  bool load(string filename);

  void run(Tracer& tracer);

  void drawGraph(Graphviz& g);

  void printTrace(ostream& o);

 private:
  shared_ptr<Command> _root;

  shared_ptr<Reference> _stdin_ref;
  shared_ptr<Artifact> _stdin;

  shared_ptr<Reference> _stdout_ref;
  shared_ptr<Artifact> _stdout;

  shared_ptr<Reference> _stderr_ref;
  shared_ptr<Artifact> _stderr;
};
