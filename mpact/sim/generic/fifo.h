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

#ifndef MPACT_SIM_GENERIC_FIFO_H_
#define MPACT_SIM_GENERIC_FIFO_H_

#include <any>
#include <cstdint>
#include <deque>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "absl/strings/string_view.h"
#include "mpact/sim/generic/arch_state.h"
#include "mpact/sim/generic/component.h"
#include "mpact/sim/generic/config.h"
#include "mpact/sim/generic/data_buffer.h"
#include "mpact/sim/generic/operand_interface.h"
#include "mpact/sim/generic/program_error.h"
#include "mpact/sim/generic/state_item.h"
#include "mpact/sim/generic/state_item_base.h"

// Contains the basic generic definitions for classes used to model
// fifo state in simulated architectures. Fifos can contain scalar,
// one-dimensional vector, or two-dimensional array values.

namespace mpact {
namespace sim {
namespace generic {

class FifoBase;

template <typename T, typename Enable = void>
class FifoSourceOperand : public SourceOperandInterface {
 public:
  // Constructor. Note, default constructor deleted.
  explicit FifoSourceOperand(FifoBase *fifo);
  FifoSourceOperand(FifoBase *fifo, const std::string op_name);
  FifoSourceOperand() = delete;

  // These accessor methods are defined to satisfy the interface. However,
  // in many cases, the purpose of the fifo is only to hold the underlying
  // DataBuffer instance, until popped in its entirety. It is therefore
  // expected that these accessors will not be used much.
  // Accessor methods call into the fifo_ data_buffer to obtain the values.
  // The index is in one dimension. For scalar registers it should always be
  // zero, for vector registers it is the element index. For higher order
  // register shapes it should be the linearized row-major order index.
  bool AsBool(int i) override;
  int8_t AsInt8(int i) override;
  uint8_t AsUint8(int i) override;
  int16_t AsInt16(int i) override;
  uint16_t AsUint16(int i) override;
  int32_t AsInt32(int i) override;
  uint32_t AsUint32(int i) override;
  int64_t AsInt64(int i) override;
  uint64_t AsUint64(int i) override;

  // Returns the FifoBase object wrapped in absl::any.
  std::any GetObject() const override { return std::any(fifo_); }

  // Returns the shape of the fifo elements.
  std::vector<int> shape() const override;

  std::string AsString() const override { return op_name_; }

 private:
  FifoBase *fifo_;
  std::string op_name_;
};

// Fifo destination operand type with element value type T. It is agnostic
// of the actual structure of the underlying fifo element (scalar, vector,
// matrix).
template <typename T>
class FifoDestinationOperand : public DestinationOperandInterface {
 public:
  // Constructors
  FifoDestinationOperand(FifoBase *fifo, int latency, std::string op_name);
  FifoDestinationOperand(FifoBase *fifo, int latency);
  FifoDestinationOperand() = delete;

  // Initializes the DataBuffer instance so that when Submit is called, it can
  // be entered into the correct delay line, with the correct latency, targeting
  // the correct fifo.
  void InitializeDataBuffer(DataBuffer *db) override;

  // Since a fifo stores multiple values, this method will return a nullptr
  // as it does not make sense to copy the value from a fifo into a destination
  // DataBuffer instance that is destined for that fifo.
  DataBuffer *CopyDataBuffer() override { return nullptr; }

  // Allocates and returns an initialized DataBuffer instance.
  DataBuffer *AllocateDataBuffer() override;

  // Returns the FifoBase object wrapped in absl::any.
  std::any GetObject() const override { return std::any(fifo_); }

  // Returns the latency associated with writes to this fifo operand.
  int latency() const override { return latency_; }

  // Returns the shape of the underlying fifo element (the number of elements in
  // each dimension). For instance {0} indicates a scalar quantity, whereas
  // {128} indicates an 128 element vector quantity.
  std::vector<int> shape() const override;

  std::string AsString() const override { return op_name_; }

 private:
  FifoBase *fifo_;
  DataBufferFactory *db_factory_;
  int latency_;
  DataBufferDelayLine *delay_line_;
  std::string op_name_;
};

// Base class for fifo types. Implements DataBufferInterface that
// the base class StateItemBase does not.
// The Fifo class supports the ability to reserve slots in the fifo for future
// pushes of data. The reserved slots are counted for the purpose of determining
// IsFull() or IsEmpty(), but are not counted in the number of Available()
// elements. The use of the reservation capability is not required to push data,
// but is implemented to provide modeling support for architectures where the
// allocation of fifo slots are separated from the actual push of data.
// No default constructor - should only be constructed from the derived classes.
class FifoBase : public StateItemBase, public Component {
  // Only constructed from derived classes.
 protected:
  FifoBase(class ArchState *arch_state, absl::string_view name,
           const std::vector<int> &shape, int element_size,
           int default_capacity);
  FifoBase() = delete;
  FifoBase(const FifoBase &) = delete;

 public:
  ~FifoBase() override;

 public:
  // Pushes the DataBuffer to the fifo provided there is space available.
  void SetDataBuffer(DataBuffer *db) override;

  // Returns true if the count of reserved and full slots equals or exceeds
  // fifo capacity.
  virtual bool IsFull() const;

  // Returns true if the fifo is empty and has zero slots reserved.
  virtual bool IsEmpty() const;

  // Returns true if the sum of reserved and full slots exceeds fifo capacity.
  // The fifo will not accept pushes once full, so the overflow can only only
  // be true if there are reserved slots.
  virtual bool IsOverSubscribed() const;

  // Reserves count slots in the fifo for future pushes. There is no check on
  // whether this causes overflow. This allows for a Reserve to be performed in
  // "parallel" with a Pop() without causing an error. If needed, an overflow
  // check should be performed at the end of the simulated cycle.
  virtual void Reserve(int count);

  // Pushes a value into the fifo, returns true if successful. Reference count
  // of the DataBuffer is incremented as part of Push. If the reserved count
  // is greater than zero it will decrement the reserved count. If the push
  // would overflow the fifo it returns false, and raises the overflow program
  // error if set.
  virtual bool Push(DataBuffer *db);

  // Returns a pointer to the DataBuffer at the front of the fifo. If the fifo
  // is empty, the nullptr is returned and the underflow program error, if set,
  // is raised.
  DataBuffer *Front() const;

  // Removes the front element of the fifo and decrements its reference count.
  // If the fifo is empty, the underflow program error, if set, is raised.
  virtual void Pop();

  // Returns max depth of fifo.
  unsigned Capacity() const { return capacity_; }

  // Returns the number of elements currently held in the fifo.
  unsigned Available() const;

  // Returns the number of reserved slots.
  unsigned Reserved() const { return reserved_; }

  // Setters for ProgramErrors
  void SetOverflowProgramError(std::unique_ptr<ProgramError> *program_error) {
    overflow_program_error_ = std::move(*program_error);
  }

  void SetUnderflowProgramError(std::unique_ptr<ProgramError> *program_error) {
    underflow_program_error_ = std::move(*program_error);
  }

 protected:
  // Configuration import.
  absl::Status ImportSelf(
      const mpact::sim::proto::ComponentData &component_data) override;

  ProgramError *overflow_program_error() {
    return overflow_program_error_.get();
  }
  ProgramError *underflow_program_error() {
    return underflow_program_error_.get();
  }

 private:
  Config<uint64_t> depth_;
  std::unique_ptr<ProgramError> overflow_program_error_;
  std::unique_ptr<ProgramError> underflow_program_error_;
  std::string name_;
  unsigned capacity_;
  unsigned reserved_;
  std::deque<DataBuffer *> fifo_;
};

// Scalar valued fifo with value type ElementType.
template <typename ElementType>
using Fifo = StateItem<FifoBase, ElementType, FifoSourceOperand<ElementType>,
                       FifoDestinationOperand<ElementType>>;

// Fifo of N long vectors with element value type ElementType.
template <typename ElementType, int N>
using VectorFifo =
    StateItem<FifoBase, ElementType, FifoSourceOperand<ElementType>,
              FifoDestinationOperand<ElementType>, N>;

// Fifo of MxN sized matrices with element value type ElementType.
template <typename ElementType, int M, int N>
using MatrixFifo =
    StateItem<FifoBase, ElementType, FifoSourceOperand<ElementType>,
              FifoDestinationOperand<ElementType>, M, N>;

template <typename T, typename Enable>
std::vector<int> FifoSourceOperand<T, Enable>::shape() const {
  return fifo_->shape();
}

// Helper templates for the partial specialiations below.
template <typename T>
using EnableIfIntegral =
    typename std::enable_if<std::is_integral<T>::value, void>::type;

template <typename T>
using EnableIfNotIntegral =
    typename std::enable_if<!std::is_integral<T>::value, void>::type;

// Helper function used in the partial specialization below.
template <
    typename F, typename T,
    typename std::enable_if<std::is_signed<T>::value, T>::type * = nullptr>
inline T HelperAs(const FifoBase *fifo, int i) {
  DataBuffer *db = fifo->Front();
  if (nullptr == db) {
    return static_cast<T>(0);
  }

  return static_cast<T>(db->Get<typename std::make_signed<F>::type>(i));
}

template <
    typename F, typename T,
    typename std::enable_if<std::is_unsigned<T>::value, T>::type * = nullptr>
inline T HelperAs(const FifoBase *fifo, int i) {
  DataBuffer *db = fifo->Front();
  if (nullptr == db) {
    return static_cast<T>(0);
  }

  return static_cast<T>(db->Get<typename std::make_unsigned<F>::type>(i));
}

template <typename T, typename Enable>
FifoSourceOperand<T, Enable>::FifoSourceOperand(FifoBase *fifo,
                                                const std::string op_name)
    : fifo_(fifo), op_name_(op_name) {}

template <typename T, typename Enable>
FifoSourceOperand<T, Enable>::FifoSourceOperand(FifoBase *fifo)
    : FifoSourceOperand(fifo, fifo->name()) {}

template <typename T>
class FifoSourceOperand<T, EnableIfIntegral<T>>
    : public SourceOperandInterface {
 public:
  // Constructors. Note, default constructor deleted.
  FifoSourceOperand(FifoBase *fifo, const std::string op_name)
      : fifo_(fifo), op_name_(op_name) {}
  explicit FifoSourceOperand(FifoBase *fifo)
      : FifoSourceOperand(fifo, fifo->name()) {}
  FifoSourceOperand() = delete;

  // These accessor methods are defined to satisfy the interface. However,
  // in many cases, the purpose of the fifo is only to hold the underlying
  // DataBuffer instance, until popped in its entirety. It is therefore
  // expected that these accessors will not be used much.
  // Accessor methods call into the fifo_ data_buffer to obtain the values.
  // The index is in one dimension. For scalar registers it should always be
  // zero, for vector registers it is the element index. For higher order
  // register shapes it should be the linearized row-major order index.
  bool AsBool(int i) override { return HelperAs<T, bool>(fifo_, i); }
  int8_t AsInt8(int i) override { return HelperAs<T, int8_t>(fifo_, i); }
  uint8_t AsUint8(int i) override { return HelperAs<T, uint8_t>(fifo_, i); }
  int16_t AsInt16(int i) override { return HelperAs<T, int16_t>(fifo_, i); }
  uint16_t AsUint16(int i) override { return HelperAs<T, uint16_t>(fifo_, i); }
  int32_t AsInt32(int i) override { return HelperAs<T, int32_t>(fifo_, i); }
  uint32_t AsUint32(int i) override { return HelperAs<T, uint32_t>(fifo_, i); }
  int64_t AsInt64(int i) override { return HelperAs<T, int64_t>(fifo_, i); }
  uint64_t AsUint64(int i) override { return HelperAs<T, uint64_t>(fifo_, i); }

  // Returns the FifoBase object wrapped in absl::any.
  std::any GetObject() const override { return std::any(fifo_); }

  // Returns the shape of the fifo elements.
  std::vector<int> shape() const override { return fifo_->shape(); }

  std::string AsString() const override { return op_name_; }

 private:
  FifoBase *fifo_;
  std::string op_name_;
};

// This is a parial specialization of the Source operand class. This is used
// when the element type stored in the data buffer is not an integral type. This
// is primarily for when the fifo element type really doesn't model a register
// value per se, but a more complex structure such as a dma descriptor. In this
// case, the value interface is not intended to be used, but instead the fifo
// is accessed by using the GetObject and casting it to the appropriate type in
// order to access the source element.
template <typename T>
class FifoSourceOperand<T, EnableIfNotIntegral<T>>
    : public SourceOperandInterface {
 public:
  // Constructors. Note, default constructor deleted.
  FifoSourceOperand(FifoBase *fifo, const std::string op_name)
      : fifo_(fifo), op_name_(op_name) {}
  explicit FifoSourceOperand(FifoBase *fifo)
      : FifoSourceOperand(fifo, fifo->name()) {}
  FifoSourceOperand() = delete;

  // These accessor methods are defined to satisfy the interface.
  bool AsBool(int) override { return false; }
  int8_t AsInt8(int i) override { return 0; }
  uint8_t AsUint8(int i) override { return 0; }
  int16_t AsInt16(int i) override { return 0; }
  uint16_t AsUint16(int i) override { return 0; }
  int32_t AsInt32(int i) override { return 0; }
  uint32_t AsUint32(int i) override { return 0; }
  int64_t AsInt64(int i) override { return 0; }
  uint64_t AsUint64(int i) override { return 0; }

  std::string AsString() const override { return fifo_->name(); }

  // Returns the FifoBase object wrapped in absl::any.
  std::any GetObject() const override { return std::any(fifo_); }

  // Returns the shape of the fifo elements.
  std::vector<int> shape() const override { return fifo_->shape(); }

 private:
  FifoBase *fifo_;
  std::string op_name_;
};

template <typename T>
FifoDestinationOperand<T>::FifoDestinationOperand(FifoBase *fifo, int latency,
                                                  std::string op_name)
    : fifo_(fifo),
      db_factory_(fifo->arch_state()->db_factory()),
      latency_(latency),
      delay_line_(fifo->arch_state()->data_buffer_delay_line()),
      op_name_(op_name) {}

template <typename T>
FifoDestinationOperand<T>::FifoDestinationOperand(FifoBase *fifo, int latency)
    : FifoDestinationOperand(fifo, latency, fifo->name()) {}

template <typename T>
void FifoDestinationOperand<T>::InitializeDataBuffer(DataBuffer *db) {
  db->set_destination(fifo_);
  db->set_latency(latency_);
  db->set_delay_line(delay_line_);
}

template <typename T>
DataBuffer *FifoDestinationOperand<T>::AllocateDataBuffer() {
  DataBuffer *db = db_factory_->Allocate(fifo_->size());
  InitializeDataBuffer(db);
  return db;
}

template <typename T>
std::vector<int> FifoDestinationOperand<T>::shape() const {
  return fifo_->shape();
}

}  // namespace generic
}  // namespace sim
}  // namespace mpact

#endif  // MPACT_SIM_GENERIC_FIFO_H_
