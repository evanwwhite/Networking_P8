#pragma once

#include "print.h"
#include "spin_lock.h"
#include <cstdint>

template <typename T> class WeakRef;
template <typename T> class UniqueRef;

namespace impl {
template <typename T> class Middle {
public:
  T *ptr;
  SpinLock lock;
  uint64_t strong_count;
  uint64_t weak_count;

  Middle(T *ptr) : ptr(ptr), strong_count(1), weak_count(0) {}

  ~Middle() {}

  static Middle *add_strong(Middle *m) {
    if (m == nullptr)
      return nullptr;
    LockGuard g{m->lock};

    if (m->strong_count > 0) {
      m->strong_count++;
      return m;
    }

    return nullptr;
  }

  static void remove_strong(Middle *m) {
    T *del_ptr = nullptr;
    bool delete_m = false;

    if (m == nullptr)
      return;

    m->lock.lock();
    ASSERT(m->strong_count > 0);
    m->strong_count--;
    if (m->strong_count == 0) {
      del_ptr = m->ptr;
      m->ptr = nullptr;
      delete_m = m->weak_count == 0;
    }
    m->lock.unlock();

    if (del_ptr != nullptr) {
      delete del_ptr;
    }

    if (delete_m) {
      delete m;
    }
  }

  static Middle *add_weak(Middle *m) {
    if (m == nullptr)
      return nullptr;
    m->lock.lock();
    if (m->ptr == nullptr) {
      m->lock.unlock();
      return nullptr;
    }
    m->weak_count++;
    m->lock.unlock();
    return m;
  }

  static Middle *remove_weak(Middle *m) {
    if (m == nullptr)
      return nullptr;
    bool delete_m = false;

    m->lock.lock();
    ASSERT(m->weak_count > 0);
    m->weak_count--;
    if (m->weak_count == 0 && m->strong_count == 0) {
      delete_m = true;
    }
    m->lock.unlock();

    if (delete_m) {
      delete m;
      return nullptr;
    } else {
      return m;
    }
  }
};
} // namespace impl

template <typename T> class StrongRef {
  using M = impl::Middle<T>;

  mutable M *middle;

  inline M *add_strong(M *m) { return M::add_strong(m); }
  inline void remove_strong(M *m) { M::remove_strong(m); }

  inline M *mm(T *ptr) {
    if (ptr == nullptr) {
      return nullptr;
    } else {
      return new M(ptr);
    }
  }

public:
  // Constructors
  StrongRef() : middle(nullptr) {}
  explicit StrongRef(T *ptr) : middle(mm(ptr)) {}
  StrongRef(const StrongRef<T> &other) : middle(add_strong(other.middle)) {}
  StrongRef(const WeakRef<T> &rhs) : middle(add_strong(rhs.middle)) {}
  StrongRef(const UniqueRef<T> &rhs) : middle(mm(rhs.ptr)) {
    rhs.ptr = nullptr;
  }

  // Destructor
  ~StrongRef() { remove_strong(middle); }

  // dereference
  T *operator->() const {
    ASSERT(middle != nullptr);
    ASSERT(middle->ptr != nullptr);
    return middle->ptr;
  }

  // comparison operators
  bool operator==(const StrongRef<T> &other) const {
    return middle == other.middle;
  }
  bool operator!=(const StrongRef<T> &other) const {
    return middle != other.middle;
  }
  bool operator==(std::nullptr_t) const {
    return (middle == nullptr) || (middle->ptr == nullptr);
  }
  bool operator!=(std::nullptr_t) const {
    return (middle != nullptr) && (middle->ptr != nullptr);
  }

  // Assignment operators
  StrongRef &operator=(std::nullptr_t) {
    remove_strong(middle);
    middle = nullptr;
    return *this;
  }
  StrongRef &operator=(const WeakRef<T> &rhs) {
    if (middle != rhs.middle) {
      remove_strong(middle);
      middle = add_strong(rhs.middle);
    }
    return *this;
  }
  StrongRef &operator=(const UniqueRef<T> &rhs) {
    remove_strong(middle);
    middle = mm(rhs.ptr);
    rhs.ptr = nullptr;
    return *this;
  }
  StrongRef &operator=(const StrongRef<T> &other) {
    if (middle != other.middle) {
      remove_strong(middle);
      middle = add_strong(other.middle);
    }
    return *this;
  }

  // factory
  template <typename... Args> static StrongRef<T> make(Args... args) {
    return StrongRef<T>{new T(args...)};
  }

  friend class WeakRef<T>;
};

template <typename T> class WeakRef {

  using M = impl::Middle<T>;

  M *middle;

  inline M *add_weak(M *m) { return M::add_weak(m); }

  inline M *remove_weak(M *m) { return M::remove_weak(m); }

public:
  // Constructors
  WeakRef() : middle(nullptr) {}

  explicit WeakRef(T *ptr) : middle(nullptr) {
    if (ptr != nullptr) {
      delete ptr;
    }
  }

  WeakRef(const WeakRef<T> &other) : middle(add_weak(other.middle)) {}
  WeakRef(const StrongRef<T> &other) : middle(add_weak(other.middle)) {}
  WeakRef(const UniqueRef<T> &rhs) : middle(nullptr) {
    if (rhs.ptr != nullptr) {
      delete rhs.ptr;
      rhs.ptr = nullptr;
    }
  }

  // Destructor
  ~WeakRef() { remove_weak(middle); }

  // Assignment operators
  WeakRef &operator=(std::nullptr_t) {
    remove_weak(middle);
    middle = nullptr;
    return *this;
  }
  WeakRef &operator=(const WeakRef<T> &other) {
    if (middle != other.middle) {
      remove_weak(middle);
      middle = add_weak(other.middle);
    }
    return *this;
  }
  WeakRef &operator=(const UniqueRef<T> &rhs) {
    remove_weak(middle);
    middle = nullptr;
    if (rhs.ptr != nullptr) {
      delete rhs.ptr;
      rhs.ptr = nullptr;
    }
    return *this;
  }
  WeakRef &operator=(const StrongRef<T> &other) {
    if (middle != other.middle) {
      remove_weak(middle);
      middle = add_weak(other.middle);
    }
    return *this;
  }

  // factory
  template <typename... Args> static WeakRef<T> make(Args... args) {
    return WeakRef<T>{new T(args...)};
  }

  // friends
  friend class StrongRef<T>;
};

template <typename T> class UniqueRef {
  mutable T *ptr;

  inline void replace(T *new_ptr) {
    if (ptr != nullptr) {
      delete ptr;
    }
    ptr = new_ptr;
  }

  friend class StrongRef<T>;
  friend class WeakRef<T>;

public:
  // Constructors
  UniqueRef() : ptr(nullptr) {}
  explicit UniqueRef(T *ptr) : ptr(ptr) {}
  UniqueRef(const UniqueRef<T> &other) : ptr(other.ptr) { other.ptr = nullptr; }
  UniqueRef(const StrongRef<T> &) : ptr(nullptr) {}
  UniqueRef(const WeakRef<T> &) : ptr(nullptr) {}

  // Destructor
  ~UniqueRef() {
    if (ptr != nullptr) {
      delete ptr;
    }
  }

  // dereference
  T *operator->() const {
    ASSERT(ptr != nullptr);
    return ptr;
  }

  // Assignment operators
  UniqueRef &operator=(std::nullptr_t) {
    replace(nullptr);
    return *this;
  }
  UniqueRef &operator=(const UniqueRef<T> &other) {
    if (ptr != other.ptr) {
      replace(other.ptr);
      other.ptr = nullptr;
    }
    return *this;
  }
  UniqueRef &operator=(const StrongRef<T> &) = delete;
  UniqueRef &operator=(const WeakRef<T> &) = delete;

  bool operator==(nullptr_t) const { return ptr == nullptr; }
  bool operator!=(nullptr_t) const { return ptr != nullptr; }
  bool operator==(const UniqueRef<T> &rhs) const { return ptr == rhs.ptr; }
  bool operator!=(const UniqueRef<T> &rhs) const { return ptr != rhs.ptr; }

  // factory
  template <typename... Args> static UniqueRef<T> make(Args... args) {
    return UniqueRef<T>{new T(args...)};
  }
};