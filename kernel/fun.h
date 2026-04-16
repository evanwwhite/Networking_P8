/* Copyright (C) 2025 Ahmed Gheith and contributors.
 *
 * Use restricted to classroom projects.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION
 * OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#pragma once

// abstract base class for callable/destrubtable objects
class Fun {
public:
  // abstract method to be overriden to define the behavior
  virtual void doit() = 0;

  // override to augment the default behavior
  // The default behavior is almost always sufficient; run destructors for all
  // base classes and members
  virtual ~Fun() = default;
};

// A concrete sub-class that implements the Fun interface for
// a callable object (function pointer, lamdba, an instance of
// a class that has an operator()() method)
template <typename Work> class FunImpl : public Fun {
public:
  // copy the object to guarantee it that it lives until
  // we call it
  Work work;

  // consturcotr
  inline FunImpl(Work work) : work(work) {}

  // do the work by calling the captured object
  void doit() override { work(); }
};
