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

#include "mpact/sim/generic/data_buffer.h"

#include <cstdint>

namespace mpact {
namespace sim {
namespace generic {

DataBufferFactory::~DataBufferFactory() { Clear(); }

// Free up all allocated DataBuffers held in the db_store.
void DataBufferFactory::Clear() {
  for (int i = 0; i <= kShortSize; i++) {
    for (auto db : short_free_list_[i]) {
      delete db;
    }
    short_free_list_[i].clear();
  }
  for (auto bucket : free_list_) {
    for (auto db : bucket.second) {
      delete db;
    }
    bucket.second.clear();
  }
}

// Allocate a DataBuffer and then copy the value of the source DB into
// the newly allocated buffer.
DataBuffer* DataBufferFactory::MakeCopyOf(const DataBuffer* src_db) {
  DataBuffer* db = Allocate(src_db->size<uint8_t>());
  db->CopyFrom(src_db);
  return db;
}

DataBuffer::DataBuffer(unsigned size) : size_(size) {
  // Allocate using unsigned char to guarantee appropriate alignment according
  // to C++ standard 5.3.4 (11).
  raw_ptr_ = static_cast<void*>(new unsigned char[size_]);
}

DataBuffer::~DataBuffer() { delete[] static_cast<unsigned char*>(raw_ptr_); }

void DataBuffer::Submit(int latency) {
  if (destination_ == nullptr) {
    DecRef();
    return;
  }
  if (0 == latency) {
    destination_->SetDataBuffer(this);
    DecRef();
  } else {
    delay_line_->Add(latency, this, destination_);
  }
}
}  // namespace generic
}  // namespace sim
}  // namespace mpact
