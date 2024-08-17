/*
 * STL-compatible shared memory allocator.
 *
 * Without Boost, support UNIX-like systems.
 * Suggest Linux >= 4.17 for safety from MAP_FIXED_NOREPLACE.
 *
 * Written by <lyazj@github.com>.
 */
#pragma once
#include <sys/stat.h>
#include <fcntl.h>
#include <string>
#include <new>

// The class contains all allocator states and operations.
class global_shared_allocator {
public:
  // Use any allocate()/deallocate() operation strictly after shm_open() and before shm_close().
  static void *allocate(size_t n);
  static void deallocate(void *p, size_t n);

  // A non-NULL `name` overrides the default name generated at start.
  // Exact one process (the master) should use `oflag & O_TRUNC` to initialize shm and the driver.
  // Argument `mode` is only significant when `oflag & O_CREAT`.
  static const char *shm_open(const char *name = NULL, int oflag = O_RDWR | O_CREAT, mode_t mode = 0600);

  // Close but keep the named shared memory file.
  static void shm_close();

  // Remove the named shared memory file.
  // Using it right after the last possible shm_open() call is recommended.
  static void shm_unlink();

  // The mode from the underlying in-memory-file inode.
  // Not necessarily equal the one passed to shm_open().
  static mode_t shm_mode();

  // The name is always available even when shm is not open.
  static const char *shm_name() { return name_.c_str(); }

  // Returns 0 if shm is not open.
  static int shm_oflag() { return oflag_; }

private:
  // These fields are local to the current process.
  static std::string name_;
  static int shmfd_;
  static int oflag_;

  // The driver lies at the very beginning of the shared memory.
  class driver;
  static class driver *driver_;
};

// A complete stateless type-specific allocator template.
// Minimum allocator requirement is implemented.
template<class T>
class shared_allocator {
public:
  // The only mandatory public type member.
  typedef T value_type;

  // We must support rebinding so explicit copy-control members are necessary.
  shared_allocator() { }
  ~shared_allocator() { }
  template<class U> shared_allocator(const shared_allocator<U> &) { }
  template<class U> shared_allocator &operator=(const shared_allocator<U> &) { return *this; }

  // See important constrains from allocate()/deallocate() in `global_shared_allocator`.
  value_type *allocate(size_t n) { return (value_type *)global_shared_allocator::allocate(n * sizeof(value_type)); }
  void deallocate(value_type *p, size_t n) { global_shared_allocator::deallocate(p, n * sizeof(value_type)); }
};

// Supports fast move-construction and move-assignment for T.
template<class T> inline bool operator==(const shared_allocator<T> &, const shared_allocator<T> &) { return true; }

// Placement new/delete operators for shared memory.
inline constexpr struct shared_t { } shared;
inline void *operator new     (size_t n, shared_t) { return global_shared_allocator::allocate(n);      }
inline void *operator new[]   (size_t n, shared_t) { return global_shared_allocator::allocate(n);      }
inline void  operator delete  (void  *p, shared_t) { return global_shared_allocator::deallocate(p, 0); }
inline void  operator delete[](void  *p, shared_t) { return global_shared_allocator::deallocate(p, 0); }
