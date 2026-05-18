#include "HarnessShm.hpp"

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>

#include "Modules/Console.hpp"

HarnessShm::HarnessShm()
    : size_(0), fd_(-1), mapped_ptr_(MAP_FAILED), initialized_(false) {}

HarnessShm::~HarnessShm() { Cleanup(); }

// docs/HarnessShm.cpp:Init>
bool HarnessShm::Init(const std::string& name, size_t size) {
  if (initialized_) {
    Cleanup();
  }

  name_ = name;
  size_ = size;

  // Open/create POSIX shared memory
  fd_ = shm_open(name_.c_str(), O_CREAT | O_RDWR, 0666);
  if (fd_ == -1) {
    console->Print("HarnessShm: Failed to shm_open %s (errno %d)\n",
                   name_.c_str(), errno);
    return false;
  }

  // Set the size
  if (ftruncate(fd_, size_) == -1) {
    console->Print("HarnessShm: Failed to ftruncate %s (errno %d)\n",
                   name_.c_str(), errno);
    shm_unlink(name_.c_str());
    close(fd_);
    fd_ = -1;
    return false;
  }

  // Map memory
  mapped_ptr_ = mmap(0, size_, PROT_READ | PROT_WRITE, MAP_SHARED, fd_, 0);
  if (mapped_ptr_ == MAP_FAILED) {
    console->Print("HarnessShm: Failed to mmap %s (errno %d)\n", name_.c_str(),
                   errno);
    shm_unlink(name_.c_str());
    close(fd_);
    fd_ = -1;
    return false;
  }

  console->Print("HarnessShm: Initialized shared memory '%s' (size: %zu)\n",
                 name_.c_str(), size_);
  initialized_ = true;
  return true;
}

void HarnessShm::Cleanup() {
  if (!initialized_) return;

  if (mapped_ptr_ != MAP_FAILED) {
    munmap(mapped_ptr_, size_);
    mapped_ptr_ = MAP_FAILED;
  }

  if (fd_ != -1) {
    close(fd_);
    fd_ = -1;
  }

  // Explicitly using a comment about Sync:
  // SYNC: We are relying on the gRPC HTTP/2 stream as the synchronization
  // barrier. C++ will write to SHM -> Send gRPC message. Python receives gRPC
  // message -> Reads SHM. This ensures no race conditions on the memory block
  // without needing complex SHM mutexes.

  shm_unlink(name_.c_str());
  initialized_ = false;

  console->Print("HarnessShm: Cleaned up shared memory '%s'\n", name_.c_str());
}
