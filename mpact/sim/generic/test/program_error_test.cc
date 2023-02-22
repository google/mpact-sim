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

#include "mpact/sim/generic/program_error.h"

#include "googlemock/include/gmock/gmock.h"

using testing::StrEq;

namespace mpact {
namespace sim {
namespace generic {
namespace {

static constexpr char kControllerName[] = "TestController";
static constexpr char kProgramError1[] = "program_error_1";
static constexpr char kMessage1[] = "message 1";

class ProgramErrorTest : public testing::Test {
 protected:
  ProgramErrorTest() {
    controller_ = new ProgramErrorController(kControllerName);
  }

  ~ProgramErrorTest() override { delete controller_; }

  ProgramErrorController *controller_;
};

TEST_F(ProgramErrorTest, ControllerInstantiation) {
  EXPECT_THAT(controller_->name(), StrEq(kControllerName));
  EXPECT_FALSE(controller_->HasError());
  EXPECT_FALSE(controller_->HasMaskedError());
  EXPECT_FALSE(controller_->HasUnmaskedError());
  EXPECT_EQ(controller_->GetMaskedErrorNames().size(), 0);
  EXPECT_EQ(controller_->GetUnmaskedErrorNames().size(), 0);
  EXPECT_TRUE(controller_->HasProgramErrorName(
      ProgramErrorController::kInternalErrorName));
}

TEST_F(ProgramErrorTest, InternalError) {
  // Trying to add a program error with the same name as the internal error name
  // will generate an internal error.
  EXPECT_FALSE(controller_->AddProgramErrorName(
      ProgramErrorController::kInternalErrorName));
  EXPECT_TRUE(controller_->HasError());
  EXPECT_FALSE(controller_->HasMaskedError());
  EXPECT_TRUE(controller_->HasUnmaskedError());
  auto errorNames = controller_->GetUnmaskedErrorNames();
  EXPECT_EQ(errorNames.size(), 1);
  EXPECT_THAT(errorNames[0], StrEq(ProgramErrorController::kInternalErrorName));
  EXPECT_EQ(controller_->GetMaskedErrorNames().size(), 0);
  EXPECT_EQ(
      controller_->GetErrorMessages(ProgramErrorController::kInternalErrorName)
          .size(),
      1);
  // Internal errors cannot be masked, so this will generate an internal error.
  controller_->Mask(ProgramErrorController::kInternalErrorName);
  EXPECT_FALSE(controller_->HasMaskedError());
  EXPECT_TRUE(controller_->HasUnmaskedError());
  EXPECT_EQ(
      controller_->GetErrorMessages(ProgramErrorController::kInternalErrorName)
          .size(),
      2);
  // Clear internal error.
  controller_->Clear(ProgramErrorController::kInternalErrorName);
  EXPECT_FALSE(controller_->HasError());
  EXPECT_FALSE(controller_->HasMaskedError());
  EXPECT_FALSE(controller_->HasUnmaskedError());
  EXPECT_EQ(
      controller_->GetErrorMessages(ProgramErrorController::kInternalErrorName)
          .size(),
      0);
  // Set another internal error and then use ClearAll()
  controller_->AddProgramErrorName(ProgramErrorController::kInternalErrorName);
  controller_->ClearAll();
  EXPECT_FALSE(controller_->HasError());
  EXPECT_FALSE(controller_->HasMaskedError());
  EXPECT_FALSE(controller_->HasUnmaskedError());
  EXPECT_EQ(
      controller_->GetErrorMessages(ProgramErrorController::kInternalErrorName)
          .size(),
      0);
}

TEST_F(ProgramErrorTest, SimpleProgramError) {
  EXPECT_FALSE(controller_->HasProgramErrorName(kProgramError1));
  EXPECT_TRUE(controller_->AddProgramErrorName(kProgramError1));
  EXPECT_TRUE(controller_->HasProgramErrorName(kProgramError1));
  auto program_error_1 = controller_->GetProgramError(kProgramError1);
  EXPECT_NE(program_error_1, nullptr);
  EXPECT_FALSE(controller_->IsMasked(kProgramError1));
  controller_->Mask(kProgramError1);
  EXPECT_TRUE(controller_->IsMasked(kProgramError1));

  // Raise masked error.
  program_error_1->Raise(kMessage1);
  EXPECT_TRUE(controller_->HasError());
  EXPECT_TRUE(controller_->HasMaskedError());
  EXPECT_FALSE(controller_->HasUnmaskedError());
  auto errorNames = controller_->GetMaskedErrorNames();
  EXPECT_EQ(errorNames.size(), 1);
  EXPECT_THAT(errorNames[0], StrEq(kProgramError1));
  auto errorMessages = controller_->GetErrorMessages(kProgramError1);
  EXPECT_EQ(errorMessages.size(), 1);
  EXPECT_THAT(errorMessages[0], StrEq(kMessage1));

  // Unmask the error.
  controller_->Unmask(kProgramError1);
  EXPECT_FALSE(controller_->IsMasked(kProgramError1));
  // The raised error should now be unmasked.
  EXPECT_TRUE(controller_->HasError());
  EXPECT_FALSE(controller_->HasMaskedError());
  EXPECT_TRUE(controller_->HasUnmaskedError());
  errorNames = controller_->GetMaskedErrorNames();
  EXPECT_EQ(errorNames.size(), 0);
  errorNames = controller_->GetUnmaskedErrorNames();
  EXPECT_EQ(errorNames.size(), 1);
  EXPECT_THAT(errorNames[0], StrEq(kProgramError1));
  errorMessages = controller_->GetErrorMessages(kProgramError1);
  EXPECT_EQ(errorMessages.size(), 1);
  EXPECT_THAT(errorMessages[0], StrEq(kMessage1));

  // Mask the error again and verify that it shows as masked.
  controller_->Mask(kProgramError1);
  EXPECT_TRUE(controller_->HasError());
  EXPECT_TRUE(controller_->HasMaskedError());
  EXPECT_FALSE(controller_->HasUnmaskedError());
  errorNames = controller_->GetMaskedErrorNames();
  EXPECT_EQ(errorNames.size(), 1);
  EXPECT_THAT(errorNames[0], StrEq(kProgramError1));
  errorMessages = controller_->GetErrorMessages(kProgramError1);
  EXPECT_EQ(errorMessages.size(), 1);
  EXPECT_THAT(errorMessages[0], StrEq(kMessage1));
  // Clear the error.
  controller_->Clear(kProgramError1);
  EXPECT_FALSE(controller_->HasError());
}

}  // namespace
}  // namespace generic
}  // namespace sim
}  // namespace mpact
