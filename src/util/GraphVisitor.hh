#pragma once

#include <map>
#include <memory>
#include <ostream>
#include <set>

#include "core/Build.hh"
#include "core/Command.hh"

using std::dynamic_pointer_cast;
using std::endl;
using std::map;
using std::ostream;
using std::pair;
using std::set;
using std::shared_ptr;
using std::to_string;

/**
 * An instance of this class is used to gather statistics as it traverses a build.
 * Usage:
 */
class GraphVisitor {
 public:
  /**
   * Print graphviz output for a completed build
   * \param b              The build to analyze
   * \param show_sysfiles  If true, include artifacts that are system files
   */
  GraphVisitor(Build& b, bool show_sysfiles) : _show_sysfiles(show_sysfiles) {
    visitCommand(b.getRoot());
  }

  /// Print the results of our stats gathering
  void print(ostream& o) {
    o << "digraph {\n";
    o << "  graph [rankdir=LR]\n";

    // Create command vertices
    for (auto& [c, id] : _commands) {
      o << "  " << id << " [label=\"" << c->getShortName() << "\" tooltip=\""
        << escape(c->getFullName()) << "\" fontname=Courier]\n";
    }

    // Create command edges
    for (auto& [parent, child] : _command_edges) {
      o << "  " << parent << " -> " << child << " [style=dotted weight=1]\n";
    }

    // Create artifact vertices
    for (auto& [a, id] : _artifacts) {
      // Start the vertex with HTML output
      o << "  " << id << " [label=<";

      // Begin a table
      o << "<table border=\"0\" cellspacing=\"0\" cellborder=\"1\" cellpadding=\"5\">";

      // Print the artifact type (not supported at the moment)
      // o << "<tr><td border=\"0\"><sub>" << ARTIFACT_TYPE << "</sub></td></tr>";

      // Special case for single-version artifacts
      if (a->getVersionCount() == 1 && a->getShortName() != "") {
        o << "<tr><td port=\"v0\">" + a->getShortName() + "</td></tr>";

      } else {
        // Add a row with the artifact name, if it has one
        if (a->getShortName() != "") {
          o << "<tr><td>" + a->getShortName() + "</td></tr>";
        }

        // Add rows for artifact versions
        for (auto& v : a->getVersions()) {
          string version_id = "v" + to_string(v.getIndex());
          o << "<tr><td port=\"" + version_id + "\"></td></tr>";
        }
      }

      // Finish the vertex line
      o << "</table>> shape=plain]\n";
    }

    // Create I/O edges
    for (auto [src, dest] : _io_edges) {
      o << "  " << src << " -> " << dest << " [arrowhead=empty weight=2]\n";
    }

    o << "}\n";
  }

  friend ostream& operator<<(ostream& o, GraphVisitor v) {
    v.print(o);
    return o;
  }

 private:
  string escape(string s) {
    auto pos = s.find('"');
    if (pos == string::npos)
      return s;
    else
      return s.substr(0, pos) + "\\\"" + escape(s.substr(pos + 1));
  }

  void visitCommand(shared_ptr<Command> c) {
    // Record this command with an ID
    _commands.emplace(c, string("c") + to_string(c->getID()));

    // Visit each of the steps the command runs
    for (auto s : c->getSteps()) {
      visitCommandStep(c, s);
    }
  };

  void visitCommandStep(shared_ptr<Command> c, shared_ptr<Step> s) {
    // Handle steps that launch new commands or access artifacts
    if (auto x = dynamic_pointer_cast<Action::Launch>(s)) {
      // Recurse into the launched command
      visitCommand(x->getCommand());

      // Add the command edge
      _command_edges.emplace(_commands[c], _commands[x->getCommand()]);

    } else if (auto x = dynamic_pointer_cast<Predicate::MetadataMatch>(s)) {
      visitInputEdge(c, x->getVersion());

    } else if (auto x = dynamic_pointer_cast<Predicate::ContentsMatch>(s)) {
      visitInputEdge(c, x->getVersion());

    } else if (auto x = dynamic_pointer_cast<Action::SetMetadata>(s)) {
      visitOutputEdge(c, x->getVersion());

    } else if (auto x = dynamic_pointer_cast<Action::SetContents>(s)) {
      visitOutputEdge(c, x->getVersion());
    }
  }

  void visitInputEdge(shared_ptr<Command> c, ArtifactVersion v) {
    if (visitArtifact(v.getArtifact())) {
      string version_id = _artifacts[v.getArtifact()] + ":v" + to_string(v.getIndex());
      _io_edges.emplace(version_id, _commands[c]);
    }
  }

  void visitOutputEdge(shared_ptr<Command> c, ArtifactVersion v) {
    if (visitArtifact(v.getArtifact())) {
      string version_id = _artifacts[v.getArtifact()] + ":v" + to_string(v.getIndex());
      _io_edges.emplace(_commands[c], version_id);
    }
  }

  bool visitArtifact(shared_ptr<Artifact> a) {
    // If this is a system file and we're not printing them, return false
    if (!_show_sysfiles & a->isSystemFile()) return false;

    // Add the artifact
    _artifacts.emplace(a, string("a") + to_string(a->getID()));
    return true;
  }

 private:
  bool _show_sysfiles;  //< Should the graph output include system files?

  /// A map from commands to their IDs in the graph output
  map<shared_ptr<Command>, string> _commands;

  /// A map from artifacts to their IDs in the graph output
  map<shared_ptr<Artifact>, string> _artifacts;

  /// A set of command edges, from parent to child
  set<pair<string, string>> _command_edges;

  /// A set of input/output edges, from source to destination (both inputs and outputs)
  set<pair<string, string>> _io_edges;
};