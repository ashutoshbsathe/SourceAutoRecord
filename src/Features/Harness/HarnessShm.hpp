#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

//docs/HarnessShm.hpp:HarnessShm>
class HarnessShm {
 public:
  HarnessShm();
  ~HarnessShm();

  bool Init(const std::string& name, size_t size);
  void Cleanup();

  void* GetBuffer() const { return mapped_ptr_; }
  size_t GetSize() const { return size_; }
  std::string GetName() const { return name_; }

 private:
  std::string name_;
  size_t size_;
  int fd_;
  void* mapped_ptr_;
  bool initialized_;
};
