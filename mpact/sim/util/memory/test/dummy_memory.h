#ifndef MPACT_SIM_UTIL_MEMORY_TEST_DUMMY_MEMORY_H_
#define MPACT_SIM_UTIL_MEMORY_TEST_DUMMY_MEMORY_H_

#include <cstdint>

#include "absl/status/status.h"
#include "mpact/sim/generic/data_buffer.h"
#include "mpact/sim/generic/instruction.h"
#include "mpact/sim/generic/ref_count.h"
#include "mpact/sim/util/memory/memory_interface.h"
#include "mpact/sim/util/memory/tagged_memory_interface.h"

namespace mpact::sim::util::test {

using ::mpact::sim::generic::DataBuffer;
using ::mpact::sim::generic::DataBufferFactory;
using ::mpact::sim::generic::Instruction;
using ::mpact::sim::generic::ReferenceCount;
using ::mpact::sim::util::AtomicMemoryOpInterface;
using ::mpact::sim::util::MemoryInterface;
using ::mpact::sim::util::TaggedMemoryInterface;

// Define a class that implements the memory interfaces, but only captures the
// memory addresses used in accesses.
class DummyMemory : public TaggedMemoryInterface,
                    public AtomicMemoryOpInterface {
 public:
  DummyMemory() = default;
  ~DummyMemory() override = default;

  // Memory interface methods.
  // Plain load.
  void Load(uint64_t address, DataBuffer* db, Instruction* inst,
            ReferenceCount* context) override {
    load_address_ = address;
  }
  // Vector load.
  void Load(DataBuffer* address_db, DataBuffer* mask_db, int el_size,
            DataBuffer* db, Instruction* inst,
            ReferenceCount* context) override {
    vector_load_address_ = address_db->Get<uint64_t>(0);
  }
  // Tagged load.
  void Load(uint64_t address, DataBuffer* db, DataBuffer* tags,
            Instruction* inst, ReferenceCount* context) override {
    tagged_load_address_ = address;
  }
  // Plain store.
  void Store(uint64_t address, DataBuffer* db) override {
    store_address_ = address;
  }
  // Vector store.
  void Store(DataBuffer* address_db, DataBuffer* mask_db, int el_size,
             DataBuffer* db) override {
    vector_store_address_ = address_db->Get<uint64_t>(0);
  }
  // Tagged store.
  void Store(uint64_t address, DataBuffer* db, DataBuffer* tags) override {
    tagged_store_address_ = address;
  }
  // Atomic memory operation.
  absl::Status PerformMemoryOp(uint64_t address, Operation op, DataBuffer* db,
                               Instruction* inst,
                               ReferenceCount* context) override {
    memory_op_address_ = address;
    return absl::OkStatus();
  }

  void ClearValues() {
    load_address_ = 0;
    store_address_ = 0;
    vector_load_address_ = 0;
    vector_store_address_ = 0;
    tagged_load_address_ = 0;
    tagged_store_address_ = 0;
    memory_op_address_ = 0;
  }

  // Accessors.
  uint64_t load_address() const { return load_address_; }
  uint64_t store_address() const { return store_address_; }
  uint64_t vector_load_address() const { return vector_load_address_; }
  uint64_t vector_store_address() const { return vector_store_address_; }
  uint64_t tagged_load_address() const { return tagged_load_address_; }
  uint64_t tagged_store_address() const { return tagged_store_address_; }
  uint64_t memory_op_address() const { return memory_op_address_; }

 private:
  uint64_t load_address_ = 0;
  uint64_t store_address_ = 0;
  uint64_t vector_load_address_ = 0;
  uint64_t vector_store_address_ = 0;
  uint64_t tagged_load_address_ = 0;
  uint64_t tagged_store_address_ = 0;
  uint64_t memory_op_address_ = 0;
};

}  // namespace mpact::sim::util::test

#endif  // MPACT_SIM_UTIL_MEMORY_TEST_DUMMY_MEMORY_H_
