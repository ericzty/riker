#include "IRBuffer.hh"

#include <cstdlib>
#include <cstring>
#include <memory>
#include <sstream>
#include <string>

#include "util/log.hh"

using std::ifstream;
using std::shared_ptr;
using std::string;
using std::stringstream;
using std::unique_ptr;

static int open_tempfile() noexcept {
  // Open an anonymous temporary file that cannot be linked to the filesystem
  int fd = ::open("/tmp", O_RDWR | O_TMPFILE | O_EXCL, 0600);
  FAIL_IF(fd < 0) << "Failed to create temporary file: " << ERR;
  return fd;
}

static string get_fd_path(int fd) {
  stringstream ss;
  ss << "/proc/self/fd/" << fd;
  return ss.str();
}

// Create an IRBuffer with a temporary file to hold buffered steps
IRBuffer::IRBuffer() noexcept :
    _id(getNextID()),
    _fd(open_tempfile()),
    _out(get_fd_path(_fd), std::ios::binary),
    _archive(_out) {}

IRBuffer::~IRBuffer() noexcept {
  ::close(_fd);
}

/// Send the stored IR trace to a sink
void IRBuffer::sendTo(IRSink& handler) noexcept {
  // Make sure this buffer is currently filling
  ASSERT(_mode == IRBuffer::Mode::Filling) << "Buffer is not ready to be drained";

  // Set the buffer to draining mode
  _mode = IRBuffer::Mode::Draining;

  _out.close();

  // Create a binary input archive to read steps
  ::lseek(_fd, 0, SEEK_SET);
  ifstream in(get_fd_path(_fd), std::ios::binary);
  cereal::BinaryInputArchive in_archive(in);

  // Send steps to the given handler
  for (size_t i = 0; i < _steps; i++) {
    unique_ptr<Record> record;
    in_archive(record);
    record->handle(*this, handler);
  }

  // Now the buffer is drained
  _mode = IRBuffer::Mode::Drained;
}

/// Identify a command with a given ID
void IRBuffer::addCommand(Command::ID id, shared_ptr<Command> cmd) noexcept {
  FAIL << "Command records should not be used in the IRBuffer data file";
}

/// Add a MetadataVersion with a known ID to this input trace
void IRBuffer::addMetadataVersion(MetadataVersion::ID id, shared_ptr<MetadataVersion> mv) noexcept {
  FAIL << "MetadataVersion records should not be used in the IRBuffer data file";
}

/// Add a ContentVersion with a known ID to this input trace
void IRBuffer::addContentVersion(ContentVersion::ID id, shared_ptr<ContentVersion> cv) noexcept {
  FAIL << "ContentVersion records should not be used in the IRBuffer data file";
}

/// Get the ID for a command instance
Command::ID IRBuffer::getCommandID(const std::shared_ptr<Command>& c) noexcept {
  auto id = c->getID(_id);
  if (id.has_value()) return id.value();

  auto iter = _command_ids.find(c);
  if (iter == _command_ids.end()) {
    // Assign an ID for the new command
    Command::ID id = _command_ids.size();

    // Add the command to the map from commands to IDs
    iter = _command_ids.emplace_hint(iter, c, id);

    // Also record the command in the map from IDs to commands
    IRLoader::addCommand(id, c);
  }

  c->setID(_id, iter->second);
  return iter->second;
}

/// Get the ID for a metadata version
MetadataVersion::ID IRBuffer::getMetadataVersionID(
    const std::shared_ptr<MetadataVersion>& mv) noexcept {
  auto id = mv->getID(_id);
  if (id.has_value()) return id.value();

  auto iter = _metadata_version_ids.find(mv);
  if (iter == _metadata_version_ids.end()) {
    // Assign an ID for the new command
    MetadataVersion::ID id = _metadata_version_ids.size();

    // Add the command to the map from commands to IDs
    iter = _metadata_version_ids.emplace_hint(iter, mv, id);

    // Also record the command in the map from IDs to commands
    IRLoader::addMetadataVersion(id, mv);
  }

  mv->setID(_id, iter->second);
  return iter->second;
}

/// Get the ID for a content version
ContentVersion::ID IRBuffer::getContentVersionID(
    const std::shared_ptr<ContentVersion>& cv) noexcept {
  auto id = cv->getID(_id);
  if (id.has_value()) return id.value();

  auto iter = _content_version_ids.find(cv);
  if (iter == _content_version_ids.end()) {
    // Assign an ID for the new command
    ContentVersion::ID id = _content_version_ids.size();

    // Add the command to the map from commands to IDs
    iter = _content_version_ids.emplace_hint(iter, cv, id);

    // Also record the command in the map from IDs to commands
    IRLoader::addContentVersion(id, cv);
  }

  cv->setID(_id, iter->second);
  return iter->second;
}