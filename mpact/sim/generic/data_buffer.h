// Copyright 2023 Google LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef MPACT_SIM_GENERIC_DATA_BUFFER_H_
#define MPACT_SIM_GENERIC_DATA_BUFFER_H_

#include <cstdint>
#include <cstring>
#include <vector>

#include "absl/container/btree_map.h"
#include "absl/types/span.h"
#include "mpact/sim/generic/delay_line.h"
#include "mpact/sim/generic/ref_count.h"

// The data buffer is used in the simulator to store the actual data
// associated with a piece of internal state such as a register or fifo.
// Since it is used to model a register, and a a register value, except for its
// width, is the epitome of untyped contents, the data buffer content similarly
// lacks strong typing, though it can be accessed using typed accessors (for
// integral types). Using a data buffer allows a decoupling of the content from
// the architecturally visibly state which can be leveraged to model register
// renaming in some architectures. It also reduces copies of data, particularly
// for long latency operations, where the result of an instruction isn't
// immediately written into the destination state object. The data buffer is
// particularly useful for large state objects such as vector registers, where
// the cost of copy is higher.
// The data buffer object supports reference counting and is automatically
// recycled upon the reference count being decremented to 0.

namespace mpact {
namespace sim {
namespace generic {

class DataBuffer;

// Factory class for DataBuffer. Note that a new data buffer can be
// either obtained through Allocate or MakeCopyOf. The latter call is useful
// when only some of the bit/bytes/words of the original value of an item of
// processor state will be modified by a semantic function.
class DataBufferFactory {
  static constexpr int kShortSize = 4096;
  friend class DataBuffer;

 public:
  // Constructor and Destructor.
  DataBufferFactory() = default;
  virtual ~DataBufferFactory();

  // Allocates a DataBuffer instance with a buffer size of num instances of T.
  // The free list is searched before falling back on using new.
  template <typename T>
  DataBuffer *Allocate(int num) {
    return Allocate(sizeof(T) * num);
  }

  // Allocates a DataBuffer instance with a buffer of size bytes. The free
  // list is searched before falling back on using new.
  inline DataBuffer *Allocate(int size);

  // Allocates a new DataBuffer instance with the same size as db and
  // initializes the contents of the internal buffer to the value of that of
  // db - without changing db. Except for the memcpy this method acts just like
  // Allocate()
  DataBuffer *MakeCopyOf(const DataBuffer *src_db);

  // Clears and frees up all the objects contained in the DBStore that holds
  // the recycled DataBuffers.
  void Clear();

 private:
  // Moves the DataBuffer instance on to a free list based on the size of the
  // internal buffer. This method is only called by DataBuffere instances.
  inline void Recycle(DataBuffer *db);
  // The DBStore keeps free DataBuffer instances in free lists by size of
  // the data_ store. That way runtime overhead is reduced, since DataBuffer
  // objects are allocated very frequently.
  //
  // map <size of data buffer data_, list of free data buffers >
  using DataBufferFreeList = absl::btree_map<int, std::vector<DataBuffer *>>;

  std::vector<DataBuffer *> short_free_list_[kShortSize + 1];
  DataBufferFreeList free_list_;
};

// DataBufferDestination is an interface that is inherited by the simulated
// state objects that use data buffers. This interface is used by the
// simulator to set a new DataBuffer object as the value of the state object,
// and in the process decrement the reference count of the previous DataBuffer
// object.

class DataBufferDestination {
 public:
  virtual void SetDataBuffer(DataBuffer *db) = 0;
  virtual ~DataBufferDestination() = default;
};

class DataBufferDelayRecord;

// Delay line type for DataBuffer instances.
using DataBufferDelayLine = DelayLine<DataBufferDelayRecord>;

namespace internal {

// Helper template to translate from type size to type.
template <int N>
struct ElementTypeSelector {};
template <>
struct ElementTypeSelector<1> {
  using type = uint8_t;
};

template <>
struct ElementTypeSelector<2> {
  using type = uint16_t;
};
template <>
struct ElementTypeSelector<4> {
  using type = uint32_t;
};
template <>
struct ElementTypeSelector<8> {
  using type = uint64_t;
};

}  // namespace internal

// DataBuffer implements the underlying data storage class for simulated
// elements of state. It has methods for accessing (set/get) elements of
// the data, manage reference counts as well Submit methods for staging the
// data buffer to be exchanged in the simulated state element (register etc.).

class DataBuffer : public ReferenceCount {
  friend class DataBufferFactory;

 public:
  ~DataBuffer() override;

  // Set entry index in the DataBuffer instance to the given value assuming
  // it contains entries of type ElementType.
  template <typename ElementType>
  inline void Set(int index, ElementType value) {
    ABSL_HARDENING_ASSERT((index + 1) * sizeof(ElementType) <= size_);
    reinterpret_cast<ElementType *>(raw_ptr_)[index] = value;
  }

  // Set entry using a span.
  template <typename ElementType>
  inline void Set(absl::Span<const ElementType> values) {
    ABSL_HARDENING_ASSERT(values.size() * sizeof(ElementType) <= size_);
    auto *data_ptr = reinterpret_cast<ElementType *>(raw_ptr_);
    for (auto const &value : values) {
      *data_ptr++ = value;
    }
  }

  // Combined Set element and Submit.
  template <typename ElementType>
  inline void SetSubmit(int index, ElementType value) {
    ABSL_HARDENING_ASSERT((index + 1) * sizeof(ElementType) <= size_);
    reinterpret_cast<ElementType *>(raw_ptr_)[index] = value;
    Submit(latency_);
  }

  // Combined Set span and Submit.
  template <typename ElementType>
  inline void SetSubmit(absl::Span<const ElementType> values) {
    ABSL_HARDENING_ASSERT(values.size() * sizeof(ElementType) <= size_);
    auto *data_ptr = reinterpret_cast<ElementType *>(raw_ptr_);
    for (auto const &value : values) {
      *data_ptr++ = value;
    }
    Submit(latency_);
  }

  // Get the value of entry index in the DataBuffer instance assuming
  // it contains entries of type ElementType.
  template <typename ElementType>
  inline ElementType Get(unsigned index) const {
    ABSL_HARDENING_ASSERT((index + 1) * sizeof(ElementType) <= size_);
    return reinterpret_cast<ElementType *>(raw_ptr_)[index];
  }

  template <int N>
  inline absl::Span<typename internal::ElementTypeSelector<N>::type> Get()
      const {
    return Get<internal::ElementTypeSelector<N>::type>();
  }

  // Return the data buffer as a span of elements of type ElementType.
  template <typename ElementType>
  inline absl::Span<ElementType> Get() const {
    return absl::MakeSpan(reinterpret_cast<ElementType *>(raw_ptr_),
                          size<ElementType>());
  }

  // Copies the content of the data buffer to the buffer stored at the
  // given location. The caller is responsible for ensuring that the
  // target buffer is of sufficient size.
  inline void CopyTo(uint8_t *data) const {
    std::memcpy(data, raw_ptr_, size_);
  }

  // Copies the content of the data stored at the given location into
  // the data buffer. The caller is responsible for ensuring that the
  // source buffer is of sufficient size.
  inline void CopyFrom(const uint8_t *data) {
    std::memcpy(raw_ptr_, data, size_);
  }

  // Copies the data from the given data buffer. The sizes have to be
  // identical.
  inline void CopyFrom(const DataBuffer *src_db) {
    ABSL_HARDENING_ASSERT(size_ == src_db->size_);
    std::memcpy(raw_ptr_, src_db->raw_ptr_, size_);
  }

  // Return the size as number of elements of type ElementType.
  template <typename ElementType>
  inline int size() const {
    return size_ / sizeof(ElementType);
  }

  // When the reference count is zero, recycle the DataBuffer instance.
  void OnRefCountIsZero() override { db_factory_->Recycle(this); }

  // Stage the DataBuffer object to update the state object. If latency is
  // not specified, it uses the latency stored in the object. If the latency
  // value is zero, the DataBufferDestination object pointed to by destination_
  // is updated immediately. Otherwise the DataBuffer object is entered into
  // a "delay line" that will update the destination after latency number of
  // cycles.
  void Submit(int latency);

  inline void Submit() { Submit(latency_); }

  // Sets the latency for the update of the DataBufferDestination object
  // with this DataBuffer instance.
  inline void set_latency(int latency) { latency_ = latency; }

  // Returns the latency value
  inline int latency() { return latency_; }

  // Sets the destination state object that will receive the data buffer upon
  // Submit(0)/0 latency, or after latency cycles from a "delay line".
  inline void set_destination(DataBufferDestination *dest) {
    destination_ = dest;
  }

  // Sets the delay line to use for this data buffer when it's submitted with
  // a non-zero latency.
  inline void set_delay_line(DataBufferDelayLine *delay_line) {
    delay_line_ = delay_line;
  }
  // Returns the raw byte pointer to the data buffer storage.
  void *raw_ptr() const { return raw_ptr_; }

 private:
  explicit DataBuffer(unsigned size);
  DataBufferFactory *db_factory_;
  DataBufferDelayLine *delay_line_;
  DataBufferDestination *destination_;
  int latency_;
  unsigned size_;
  void *raw_ptr_;
};

// DataBufferDelayRecord is used as the parameter for the DelayLine for
// DataBuffer instances that are being written back to instances of simulated
// state.
class DataBufferDelayRecord {
 public:
  DataBufferDelayRecord() = delete;
  DataBufferDelayRecord(const DataBufferDelayRecord &rhs) {
    dest_ = rhs.dest_;
    data_buffer_ = rhs.data_buffer_;
    data_buffer_->IncRef();
  }
  DataBufferDelayRecord(DataBuffer *data_buffer, DataBufferDestination *dest)
      : data_buffer_(data_buffer), dest_(dest) {}
  ~DataBufferDelayRecord() {
    if (data_buffer_ != nullptr) {
      data_buffer_->DecRef();
      data_buffer_ = nullptr;
    }
  }
  void Apply() { dest_->SetDataBuffer(data_buffer_); }

 private:
  DataBuffer *data_buffer_;
  DataBufferDestination *dest_;
};

// Put the DataBuffer into the db_store based on size of the data it can hold.
// This method is only call from DataBuffer instance, so no need to check for
// db == nullptr.
inline void DataBufferFactory::Recycle(DataBuffer *db) {
  int size = db->size<uint8_t>();
  if (size <= kShortSize) {
    short_free_list_[size].push_back(db);
  } else {
    auto ptr = free_list_.find(size);
    if (ptr != free_list_.end()) {
      ptr->second.push_back(db);
      return;
    }
    free_list_.emplace(size, std::vector<DataBuffer *>{db});
  }
}

// Search through the DataBuffer recycled store to see if there is a buffer
// of the appropriate size. If not, allocate a new one.
inline DataBuffer *DataBufferFactory::Allocate(int size) {
  DataBuffer *db = nullptr;
  {
    if (size <= kShortSize) {
      auto &free_list = short_free_list_[size];
      if (!free_list.empty()) {
        db = free_list.back();
        free_list.pop_back();
        db->IncRef();
        return db;
      }
    } else {
      auto ptr = free_list_.find(size);
      if (ptr != free_list_.end()) {
        if (!ptr->second.empty()) {
          db = ptr->second.back();
          ptr->second.pop_back();
          db->IncRef();
          return db;
        }
      }
    }
  }

  db = new DataBuffer(size);
  db->db_factory_ = this;

  return db;
}

}  // namespace generic
}  // namespace sim
}  // namespace mpact

#endif  // MPACT_SIM_GENERIC_DATA_BUFFER_H_
