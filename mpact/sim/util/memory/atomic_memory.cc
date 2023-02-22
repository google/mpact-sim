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

#include "mpact/sim/util/memory/atomic_memory.h"

#include <cstring>

#include "absl/log/log.h"
#include "absl/status/status.h"

namespace mpact {
namespace sim {
namespace util {

// Helper function that writes the value to the data buffer.
template <typename I>
static absl::Status WriteDb(DataBuffer *db, I value) {
  switch (db->size<uint8_t>()) {
    case 1:
      db->Set<uint8_t>(0, static_cast<uint8_t>(value));
      break;
    case 2:
      db->Set<uint16_t>(0, static_cast<uint16_t>(value));
      break;
    case 4:
      db->Set<uint32_t>(0, static_cast<uint32_t>(value));
      break;
    case 8:
      db->Set<uint64_t>(0, static_cast<uint64_t>(value));
      break;
    default:
      return absl::InternalError(
          absl::StrCat("Illegal element size (", db->size<uint8_t>(), ")"));
  }
  return absl::OkStatus();
}

// Helper function that performs the given atomic memory operation on the data
// buffers.
template <typename T>
static void PerformOp(AtomicMemoryOpInterface::Operation op,
                      const DataBuffer *db_lhs, const DataBuffer *db_rhs,
                      DataBuffer *db_res) {
  using UT = typename std::make_unsigned<T>::type;
  using ST = typename std::make_signed<T>::type;
  switch (op) {
    case AtomicMemoryOpInterface::Operation::kAtomicAdd:
      db_res->Set<T>(0, db_lhs->Get<T>(0) + db_rhs->Get<T>(0));
      break;
    case AtomicMemoryOpInterface::Operation::kAtomicSub:
      db_res->Set<T>(0, db_lhs->Get<T>(0) - db_rhs->Get<T>(0));
      break;
    case AtomicMemoryOpInterface::Operation::kAtomicAnd:
      db_res->Set<T>(0, db_lhs->Get<T>(0) & db_rhs->Get<T>(0));
      break;
    case AtomicMemoryOpInterface::Operation::kAtomicOr:
      db_res->Set<T>(0, db_lhs->Get<T>(0) | db_rhs->Get<T>(0));
      break;
    case AtomicMemoryOpInterface::Operation::kAtomicXor:
      db_res->Set<T>(0, db_lhs->Get<T>(0) ^ db_rhs->Get<T>(0));
      break;
    case AtomicMemoryOpInterface::Operation::kAtomicMax:
      db_res->Set<T>(0, std::max(db_lhs->Get<ST>(0), db_rhs->Get<ST>(0)));
      break;
    case AtomicMemoryOpInterface::Operation::kAtomicMaxu:
      db_res->Set<T>(0, std::max(db_lhs->Get<UT>(0), db_rhs->Get<UT>(0)));
      break;
    case AtomicMemoryOpInterface::Operation::kAtomicMin:
      db_res->Set<T>(0, std::min(db_lhs->Get<ST>(0), db_rhs->Get<ST>(0)));
      break;
    case AtomicMemoryOpInterface::Operation::kAtomicMinu:
      db_res->Set<T>(0, std::min(db_lhs->Get<UT>(0), db_rhs->Get<UT>(0)));
      break;
    default:
      LOG(ERROR) << absl::StrCat("Unhandled case in PerformOp (",
                                 static_cast<int>(op), ")");
  }
}

// Constructor.
AtomicMemory::AtomicMemory(MemoryInterface *memory) : memory_(memory) {
  // Construct and initialize the local data buffers.
  db1_ = db_factory_.Allocate<uint8_t>(1);
  db1_->set_latency(0);
  db1_->set_destination(nullptr);
  db2_ = db_factory_.Allocate<uint16_t>(1);
  db2_->set_latency(0);
  db2_->set_destination(nullptr);
  db4_ = db_factory_.Allocate<uint32_t>(1);
  db4_->set_latency(0);
  db4_->set_destination(nullptr);
  db8_ = db_factory_.Allocate<uint64_t>(1);
  db8_->set_latency(0);
  db8_->set_destination(nullptr);
}

// Destructor.
AtomicMemory::~AtomicMemory() {
  db1_->DecRef();
  db2_->DecRef();
  db4_->DecRef();
  db8_->DecRef();
}

// Forward the load call.
void AtomicMemory::Load(uint64_t address, DataBuffer *db, Instruction *inst,
                        ReferenceCount *context) {
  memory_->Load(address, db, inst, context);
}

// Forward the load call.
void AtomicMemory::Load(DataBuffer *address_db, DataBuffer *mask_db,
                        int el_size, DataBuffer *db, Instruction *inst,
                        ReferenceCount *context) {
  memory_->Load(address_db, mask_db, el_size, db, inst, context);
}

// Store the value to memory, but remove any matching tag from the tag set.
void AtomicMemory::Store(uint64_t address, DataBuffer *db) {
  // If the address matches a tag, remove the tag.
  auto ptr = ll_tag_set_.find(address >> kTagShift);
  if (ptr != ll_tag_set_.end()) {
    ll_tag_set_.erase(ptr);
  }
  memory_->Store(address, db);
}

// Store the value to memory, but remove any matching tag from the tag set.
void AtomicMemory::Store(DataBuffer *address_db, DataBuffer *mask_db,
                         int el_size, DataBuffer *db) {
  // If the address matches a tag, remove the tag.
  for (uint64_t address : address_db->Get<uint64_t>()) {
    auto ptr = ll_tag_set_.find(address >> kTagShift);
    if (ptr != ll_tag_set_.end()) {
      ll_tag_set_.erase(ptr);
    }
  }
  memory_->Store(address_db, mask_db, el_size, db);
}

// Perform the atomic memory operation.
absl::Status AtomicMemory::PerformMemoryOp(uint64_t address, Operation op,
                                           DataBuffer *db, Instruction *inst,
                                           ReferenceCount *context) {
  int el_size = db->size<uint8_t>();
  db->set_latency(0);
  // Load-linked.
  if (op == Operation::kLoadLinked) {
    uint64_t tag = address >> kTagShift;
    if (ll_tag_set_.find(tag) == ll_tag_set_.end()) {
      ll_tag_set_.insert(tag);
    }
    memory_->Load(address, db, inst, context);
    return absl::OkStatus();
  }
  // Store-conditional.
  if (op == Operation::kStoreConditional) {
    uint64_t tag = address >> kTagShift;
    int value = 1;
    // Determine if the store is successful.
    if (ll_tag_set_.find(tag) != ll_tag_set_.end()) {
      // Successful SC. Store the value and set result to 0.
      memory_->Store(address, db);
      ll_tag_set_.erase(tag);
      value = 0;
    }
    auto res = WriteDb(db, value);
    if (!res.ok()) return res;
    WriteBack(inst, context, db);
    return absl::OkStatus();
  }

  // Load the data from memory.
  auto *tmp_db = GetDb(el_size);
  if (tmp_db == nullptr)
    return absl::InternalError(
        absl::StrCat("Illegal element size (", el_size, ")"));

  memory_->Load(address, tmp_db, nullptr, nullptr);

  // Swap with store data - this way the data from the memory is in the right
  // data buffer (i.e., being written back to the register).
  switch (db->size<uint8_t>()) {
    case 1:
      std::swap(tmp_db->Get<uint8_t>()[0], db->Get<uint8_t>()[0]);
      break;
    case 2:
      std::swap(tmp_db->Get<uint16_t>()[0], db->Get<uint16_t>()[0]);
      break;
    case 4:
      std::swap(tmp_db->Get<uint32_t>()[0], db->Get<uint32_t>()[0]);
      break;
    case 8:
      std::swap(tmp_db->Get<uint64_t>()[0], db->Get<uint64_t>()[0]);
      break;
  }
  if (op == Operation::kAtomicSwap) {
    memory_->Store(address, tmp_db);
    WriteBack(inst, context, db);
    return absl::OkStatus();
  }

  // Other atomic operations on memory and load/store data. Notice that the
  // memory value is in db, and the load/store data is in tmp_db.
  switch (db->size<uint8_t>()) {
    case 1:
      PerformOp<uint8_t>(op, /*db_lhs*/ db, /*db_rhs*/ tmp_db,
                         /*db_res*/ tmp_db);
      break;
    case 2:
      PerformOp<uint16_t>(op, /*db_lhs*/ db, /*db_rhs*/ tmp_db,
                          /*db_res*/ tmp_db);
      break;
    case 4:
      PerformOp<uint32_t>(op, /*db_lhs*/ db, /*db_rhs*/ tmp_db,
                          /*db_res*/ tmp_db);
      break;
    case 8:
      PerformOp<uint64_t>(op, /*db_lhs*/ db, /*db_rhs*/ tmp_db,
                          /*db_res*/ tmp_db);
      break;
  }
  // Store the new value to memory, the other value will be written back to the
  // instruction destination.
  memory_->Store(address, tmp_db);
  WriteBack(inst, context, db);
  return absl::OkStatus();
}

void AtomicMemory::WriteBack(Instruction *inst, ReferenceCount *context,
                             DataBuffer *db) {
  if (inst != nullptr) {
    if (db->latency() > 0) {
      inst->IncRef();
      if (context != nullptr) context->IncRef();
      inst->state()->function_delay_line()->Add(db->latency(),
                                                [inst, context]() {
                                                  inst->Execute(context);
                                                  if (context != nullptr)
                                                    context->DecRef();
                                                  inst->DecRef();
                                                });
    } else {
      inst->Execute(context);
    }
  }
}

DataBuffer *AtomicMemory::GetDb(int size) const {
  switch (size) {
    case 1:
      return db1_;
    case 2:
      return db2_;
    case 4:
      return db4_;
    case 8:
      return db8_;
    default:
      return nullptr;
  }
}

}  // namespace util
}  // namespace sim
}  // namespace mpact
