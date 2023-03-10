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

#ifndef MPACT_SIM_DECODER_BASE_CLASS_H_
#define MPACT_SIM_DECODER_BASE_CLASS_H_

#include "mpact/sim/decoder/template_expression.h"

namespace mpact {
namespace sim {
namespace machine_description {
namespace instruction_set {

// This template class is a helper in using base classes in the decoder
// description, such as base slots.

template <typename T>
struct BaseClass {
  const T *base;
  TemplateInstantiationArgs *arguments = nullptr;
  BaseClass(const T *base_, TemplateInstantiationArgs *arguments_)
      : base(base_), arguments(arguments_) {}
  explicit BaseClass(const T *base_) : base(base_) {}
};

}  // namespace instruction_set
}  // namespace machine_description
}  // namespace sim
}  // namespace mpact
#endif  // MPACT_SIM_DECODER_BASE_CLASS_H_
