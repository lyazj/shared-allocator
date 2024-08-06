#include "shared_allocator.h"
#include <semaphore.h>
#include <stdexcept>
#include <system_error>
#include <string>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <errno.h>
#include <sys/mman.h>

using namespace std;

string global_shared_allocator::name_ = to_string(getpid()) + ".shm";;
int global_shared_allocator::shmfd_ = -1;
int global_shared_allocator::oflag_;
global_shared_allocator::driver *global_shared_allocator::driver_;

static system_error make_system_error(const string &what) { return {errno, system_category(), what}; }

class global_shared_allocator::driver {
public:
  // This puts the driver at the beginning of the shared memory.
  static void create();

  // This mustn't be a non-static member, otherwise it would destroy `this`.
  static void destroy(driver *);

  void *allocate(size_t n);
  void deallocate(void *p, size_t n);

private:
  driver(size_t size);
  ~driver();

  // Semaphores may support inter-process better than pthreads.
  sem_t sem_;

  // Mapping address of the shared memory. It must be the same among sharing processes.
  void *addr_;

  // Allocated (truncated) in-memory-file size.
  size_t size_;

  // The size limit is considered acceptable for typical cases.
  // A larger size setting can cause a `mmap()` failure on some systems.
  static inline constexpr size_t max_size_ = (size_t)1 << (sizeof(size_t) == 8 ? 32 : 30);

  // 4096 is a typical page size. It is hard-coded for simplicity. Use `getpagesize()` instead on demand.
  static inline constexpr size_t min_size_ = 4096;

  // 16 is a typical align for `malloc()`. Change it on demand.
  static inline constexpr size_t data_align_ = 16;

  // Take the alignment as the minimal data (payload) size.
  static inline constexpr size_t min_data_size_ = data_align_;

  // Take #bits of `size_t` as the number of free lists.
  static inline constexpr size_t n_free_list_ = sizeof(size_t) << 3;

  // In-place memory management.
  struct chunk;

  // We force the alignment of header and footer for addressing convenience.
  struct chunk_header {
    size_t size_;
    chunk *prev_;
  } __attribute__((aligned(data_align_)));

  // We split prev and next pointers between header and footer for space efficiency.
  struct chunk_footer {
    size_t size_;  // unallocated: mirror of header->size_; allocated: 0
    chunk *next_;
  } __attribute__((aligned(data_align_)));

  struct chunk {
    // The placeholders make `free_list_` elements accessible as real `chunk`s.
    chunk_header header_placeholder;
    chunk_footer footer_placeholder;

    // Initialization and localization.
    static chunk *add_chunk(void *addr, size_t size);
    static chunk *get_chunk(void *data);
    static size_t list_index(size_t size);  // size != 0

    // Attribute and associated-element accessing.
    bool allocated() const;
    size_t size() const, full_size() const;
    chunk_header *header() { return (chunk_header *)((const chunk *)this)->header(); }
    chunk_footer *footer() { return (chunk_footer *)((const chunk *)this)->footer(); }
    const chunk_header *header() const;
    const chunk_footer *footer() const;
    chunk *before(), *after();  // implicit doubly linked list by address space
    void *data() const;

    // Free list Manipulation.
    void allocate(size_t reqsize), deallocate();
    void add(), remove(), split(size_t remsize);
    chunk *coalesce();
  } free_list_[n_free_list_];  // dummy head

  // The addition is safe as the two pars are the same aligned.
  static inline constexpr size_t min_chunk_size_ = sizeof(chunk) + min_data_size_;

  // Underlying linear memory management.
  static int map_prot();
  chunk *extend(size_t size);

  // Concurrence control.
  struct lock {
    lock() { if(sem_wait(&driver_->sem_)) throw make_system_error("sem_wait"); }
    ~lock() noexcept(false) { if(sem_post(&driver_->sem_)) throw make_system_error("sem_post"); }
  };

} __attribute__((aligned(data_align_)));  // This makes &driver_[1] a safe address of the first chunk.

void *global_shared_allocator::allocate(size_t n) { return driver_->allocate(n); }
void global_shared_allocator::deallocate(void *p, size_t n) { return driver_->deallocate(p, n); }

const char *global_shared_allocator::shm_open(const char *name, int oflag, mode_t mode)
{
  if(driver_) throw logic_error("duplicate call to "s + __func__);
  if(name) name_ = name;
  oflag_ = oflag;
  shmfd_ = ::shm_open(name_.c_str(), oflag_, mode);
  if(shmfd_ < 0) throw make_system_error("shm_open");
  driver::create();
  // We keep shmfd_ open for future file manipulation support.
  return shm_name();
}

void global_shared_allocator::shm_close()
{
  if(!driver_) throw logic_error("invalid call to "s + __func__);
  driver::destroy(driver_);
  driver_ = NULL;
  close(shmfd_);
  shmfd_ = -1;
  oflag_ = 0;
}

void global_shared_allocator::shm_unlink()
{
  if(::shm_unlink(name_.c_str())) throw make_system_error("shm_unlink");
}

mode_t global_shared_allocator::shm_mode()
{
  struct stat st;
  if(fstat(shmfd_, &st)) throw make_system_error("fstat");
  return st.st_mode;
}

void global_shared_allocator::driver::create()
{
  static_assert(sizeof(driver) <= min_size_);

  // Get original shared memory size.
  struct stat st;
  if(fstat(shmfd_, &st)) throw make_system_error("fstat");
  size_t size = st.st_size;

  // Allocate at least min_size_ bytes.
  if(size > max_size_) throw logic_error("shared memory too large: "s + to_string(size) + " bytes");
  if(size < min_size_) {
    if(ftruncate(shmfd_, min_size_)) throw make_system_error("ftruncate");
    size = min_size_;
  }

  // Map shared memory.
  // max_size_ bytes are mapped for safety consideration. See man mmap(2).
  void *addr = mmap(NULL, max_size_, map_prot(), MAP_SHARED, shmfd_, 0);
  if(addr == MAP_FAILED) throw make_system_error("mmap");

  // Create driver at the beginning of the shared memory.
  driver_ = (driver *)addr;
  if(oflag_ & O_TRUNC) {  // master
    new(addr) driver(size);
  } else {
    // Make sure every process maps the same address.
    void *hint = driver_->addr_;
    if(hint != addr) {
      if(munmap(addr, size)) throw make_system_error("munmap");
      addr = mmap(hint, max_size_, map_prot(), MAP_SHARED | MAP_FIXED_NOREPLACE, shmfd_, 0);
      if(addr != hint) throw make_system_error("mmap");
    }
  }
}

void global_shared_allocator::driver::destroy(driver *instance)
{
  // Destroy driver.
  if(oflag_ & O_TRUNC) instance->~driver();

  // Unmap shared memory.
  if(munmap(instance, max_size_)) throw make_system_error("munmap");
}

global_shared_allocator::driver::driver(size_t size)
{
  if(sem_init(&sem_, 1, 1)) throw make_system_error("sem_init");
  addr_ = this;
  size_ = size;
  memset(free_list_, 0, sizeof free_list_);
  size -= sizeof *this;
  if(size >= min_chunk_size_) chunk::add_chunk(&this[1], size);
}

global_shared_allocator::driver::~driver()
{
  // empty
}

void *global_shared_allocator::driver::allocate(size_t size)
{
  if(size == 0) return NULL;

  size = (size + data_align_ - 1) & ~(data_align_ - 1);
  lock l;
  for(size_t i = chunk::list_index(size); i < n_free_list_; ++i) {
    chunk *c = free_list_[i].footer()->next_;
    while(c) {
      if(c->size() >= size) {
        c->allocate(size);
        return c->data();
      }
      c = c->footer()->next_;
    }
  }
  chunk *c = extend(size + sizeof(chunk));
  c->allocate(size);
  return c->data();
}

void global_shared_allocator::driver::deallocate(void *p, size_t)
{
  if(!p) return;
  lock l;
  chunk *c = chunk::get_chunk(p);
  c->deallocate();
}

global_shared_allocator::driver::chunk *global_shared_allocator::driver::chunk::add_chunk(void *addr, size_t size)
{
  if(size & (data_align_ - 1)) throw logic_error("add_chunk: size unaligned");
  if(size < min_chunk_size_) throw logic_error("add_chunk: size too small");
  chunk *c = (chunk *)addr;
  c->header()->size_ = size - sizeof(chunk);
  c->header()->prev_ = NULL;
  c->footer()->size_ = c->header()->size_;  // unallocated
  c->footer()->next_ = NULL;
  return c->coalesce();
}

global_shared_allocator::driver::chunk *global_shared_allocator::driver::chunk::get_chunk(void *data)
{
  if((uintptr_t)data & (data_align_ - 1)) throw logic_error("get_chunk: data unaligned");
  return (chunk *)((chunk_header *)data - 1);
}

size_t global_shared_allocator::driver::chunk::list_index(size_t size)
{
  if(size == 0) throw logic_error("list_index: zero size");
  unsigned long long s = {size};  // This avoids narrowing.
  return 63 - __builtin_clzll(s);
}

bool global_shared_allocator::driver::chunk::allocated() const
{
  return footer()->size_ == 0;
}

size_t global_shared_allocator::driver::chunk::size() const
{
  return header()->size_;
}

size_t global_shared_allocator::driver::chunk::full_size() const
{
  return size() + sizeof(chunk);
}

const global_shared_allocator::driver::chunk_header *global_shared_allocator::driver::chunk::header() const
{
  return (const chunk_header *)this;
}

const global_shared_allocator::driver::chunk_footer *global_shared_allocator::driver::chunk::footer() const
{
  return (const chunk_footer *)((uintptr_t)this + full_size()) - 1;
}

global_shared_allocator::driver::chunk *global_shared_allocator::driver::chunk::before()
{
  if(this == (chunk *)&driver_[1]) return NULL;
  chunk_footer *footer = (chunk_footer *)this - 1;
  if(footer->size_ == 0) return NULL;  // allocated
  void *data = (void *)((uintptr_t)footer - footer->size_);
  return get_chunk(data);
}

global_shared_allocator::driver::chunk *global_shared_allocator::driver::chunk::after()
{
  chunk *c = (chunk *)&footer()[1];
  if((uintptr_t)c + min_chunk_size_ > (uintptr_t)driver_ + driver_->size_) return NULL;
  if(c->allocated()) return NULL;
  return c;
}

void *global_shared_allocator::driver::chunk::data() const
{
  return (void *)&header()[1];
}

void global_shared_allocator::driver::chunk::allocate(size_t reqsize)
{
  if(reqsize & (data_align_ - 1)) throw logic_error("allocate: size unaligned");
  if(size() < reqsize) throw logic_error("allocate: size too small");
  remove();
  size_t remsize = size() - reqsize;
  if(remsize >= min_chunk_size_) {
    split(remsize);
  } else {
    footer()->size_ = 0;
  }
}

void global_shared_allocator::driver::chunk::deallocate()
{
  if(footer()->size_) throw logic_error("deallocate: unexpected footer size");
  footer()->size_ = header()->size_;
  coalesce();
}

void global_shared_allocator::driver::chunk::add()
{
  size_t i = list_index(size());
  chunk *p = &driver_->free_list_[i];
  chunk *n = p->footer()->next_;
  p->footer()->next_ = this;
  header()->prev_ = p;
  footer()->next_ = n;
  if(n) n->header()->prev_ = this;
}

void global_shared_allocator::driver::chunk::remove()
{
  chunk *p = header()->prev_;
  chunk *n = footer()->next_;
  header()->prev_ = NULL;
  footer()->next_ = NULL;
  p->footer()->next_ = n;
  if(n) n->header()->prev_ = p;
}

void global_shared_allocator::driver::chunk::split(size_t remsize)
{
  if(remsize < min_chunk_size_) throw logic_error("split: size too small");
  if(remsize & (data_align_ - 1)) throw logic_error("split: size unaligned");
  header()->size_ -= remsize;
  footer()->size_ = 0;
  footer()->next_ = NULL;
  add_chunk(&footer()[1], remsize);
}

global_shared_allocator::driver::chunk *global_shared_allocator::driver::chunk::coalesce()
{
  chunk *b = before(), *a = after();
  if(!a && !b) { add(); return this; }
  if(b) b->remove();
  if(a) a->remove();
  size_t new_size = full_size() + (b ? b->full_size() : 0) + (a ? a->full_size() : 0);
  if(!b) b = this;
  return add_chunk(b, new_size);
}

global_shared_allocator::driver::chunk *global_shared_allocator::driver::extend(size_t size)
{
  size_t s = size_;
  while(s < max_size_ && s - size_ < size) s *= 2;
  if(s - size_ < size) throw bad_alloc();
  size = s - size_;

  if(ftruncate(shmfd_, s)) throw make_system_error("ftruncate");
  chunk *c = (chunk *)((char *)this + size_);
  size_ = s;
  return chunk::add_chunk(c, size);
}

int global_shared_allocator::driver::map_prot()
{
  int prot = PROT_READ;
  if(oflag_ & O_WRONLY) prot &= ~PROT_READ;
  if(oflag_ & O_RDWR) prot |= PROT_WRITE;
  return prot;
}
