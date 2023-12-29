// -*- c++ -*-
// Containers that can store in an arena.

#include <cassert>
#include <cstdint>

#include "arena.h"

// Expected to be allocated in itself in an Arena.
//
// This allows to append new values and iterate through them. No removal
// possible. Random access provided, but will behave O(N).
//
// Allocates blocks starting with MIN_BLOCK_SIZE and doubles on earch new
// block, but does not exeed MAX_BLOCK_SIZE.
//
// With MIN_BLOCK_SIZE=1, MAX_BLOCK_SIZE=1, this behaves as linked-list.

template <typename T, uint16_t MIN_BLOCK_SIZE = 1, uint16_t MAX_BLOCK_SIZE = 64>
class ArenaDeque {
 private:
  struct Block {
    Block *next = nullptr;
    T value[MIN_BLOCK_SIZE];
  };
  struct BlockSizeTracker;

 public:
  using value_type = T;

  ArenaDeque() : current_(&top_) {}

  T &Append(T value, Arena *arena) {
    if (next_block_pos_ >= block_size_.current()) {
      const size_t new_N = block_size_.AdvanceNextBounded();
      // Allocate enough so that we can write beyond the 'nominal' end of value
      current_->next = (Block *)arena->Alloc(
        sizeof(Block) + sizeof(T) * (new_N - MIN_BLOCK_SIZE));
      current_->next->next = nullptr;
      current_ = current_->next;
      next_block_pos_ = 0;
    }
    T &location = current_->value[next_block_pos_];
    ++next_block_pos_;
    location = value;
    ++size_;
    return location;
  }

  // Slow: first ccouple of SIZE values are O(1), but further down O(N)
  const T &operator[](size_t pos) const {
    const Block *access_block = &top_;
    BlockSizeTracker size_choice;
    while (pos >= size_choice.current()) {
      access_block = access_block->next;
      pos -= size_choice.current();
      size_choice.AdvanceNextBounded();
    }
    return access_block->value[pos];
  }

  size_t size() const { return size_; }

  class const_iterator {
   public:
    const T &operator*() {
      assert(block_ != nullptr);
      return block_->value[pos_];
    }
    const_iterator &operator++() {
      ++pos_;
      if (pos_ >= block_size_.current()) {
        block_ = block_->next;
        pos_ = 0;
        block_size_.AdvanceNextBounded();
      }
      return *this;
    }
    bool operator==(const const_iterator &other) const {
      return other.block_ == block_ && other.pos_ == pos_;
    }
    bool operator!=(const const_iterator &other) const {
      return !(*this == other);
    }

   private:
    friend class ArenaDeque;
    const_iterator(const Block *block, size_t pos) : block_(block), pos_(pos) {}
    const Block *block_;
    size_t pos_;
    BlockSizeTracker block_size_;
  };

  const_iterator begin() const { return const_iterator(&top_, 0); }
  const_iterator end() const {
    return next_block_pos_ == block_size_.current()
             ? const_iterator(nullptr, 0)
             : const_iterator(current_, next_block_pos_);
  }

 private:
  size_t size_ = 0;
  Block top_;  // first block allocated with class
  Block *current_;
  uint16_t next_block_pos_ = 0;

  struct BlockSizeTracker {
    uint8_t size_shift = 0;
    uint16_t current() const { return (MIN_BLOCK_SIZE << size_shift); }
    uint16_t AdvanceNextBounded() {
      if ((MIN_BLOCK_SIZE << (size_shift + 1)) <= MAX_BLOCK_SIZE) {
        ++size_shift;
      }
      return MIN_BLOCK_SIZE << size_shift;
    }
  };
  BlockSizeTracker block_size_;
};
