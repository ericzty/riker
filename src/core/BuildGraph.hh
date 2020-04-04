#pragma once

#include <list>
#include <map>
#include <memory>
#include <string>

#include "core/Artifact.hh"
#include "ui/options.hh"

class Command;
class Graphviz;
class Tracer;

using std::list;
using std::map;
using std::string;
using std::unique_ptr;

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

  void prune();

  void drawGraph(Graphviz& g);

  void printTrace(ostream& o);

 private:
  shared_ptr<Command> _root;
};
