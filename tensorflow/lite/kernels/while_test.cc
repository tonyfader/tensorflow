/* Copyright 2019 The TensorFlow Authors. All Rights Reserved.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
==============================================================================*/
#include <stdint.h>

#include <memory>
#include <vector>

#include <gtest/gtest.h>
#include "tensorflow/lite/core/interpreter.h"
#include "tensorflow/lite/kernels/subgraph_test_util.h"
#include "tensorflow/lite/profiling/memory_info.h"

namespace tflite {

using subgraph_test_util::CheckIntTensor;
using subgraph_test_util::CheckScalarStringTensor;
using subgraph_test_util::CheckStringTensor;
using subgraph_test_util::ControlFlowOpTest;
using subgraph_test_util::FillIntTensor;
using subgraph_test_util::FillScalarStringTensor;

namespace {

class WhileTest : public ControlFlowOpTest {};

// The test builds a model that produces the i-th number of
// triangular number sequence.
TEST_F(WhileTest, TestTriangularNumberSequence) {
  const std::vector<int> expected = {1, 3, 6, 10, 15, 21, 28};
  for (int i = 0; i < expected.size(); ++i) {
    interpreter_ = std::make_unique<Interpreter>();
    AddSubgraphs(2);
    builder_->BuildLessEqualCondSubgraph(interpreter_->subgraph(1), i);
    builder_->BuildAccumulateLoopBodySubgraph(interpreter_->subgraph(2));
    builder_->BuildWhileSubgraph(&interpreter_->primary_subgraph());

    interpreter_->ResizeInputTensor(interpreter_->inputs()[0], {1});
    interpreter_->ResizeInputTensor(interpreter_->inputs()[1], {1});
    ASSERT_EQ(interpreter_->AllocateTensors(), kTfLiteOk);
    FillIntTensor(interpreter_->tensor(interpreter_->inputs()[0]), {1});
    FillIntTensor(interpreter_->tensor(interpreter_->inputs()[1]), {1});

    // Check While BODY inputs are static tensors.
    auto body_subgraph = interpreter_->subgraph(2);
    TfLiteTensor* subgraph_input2 =
        body_subgraph->tensor(body_subgraph->inputs()[1]);
    ASSERT_EQ(subgraph_input2->allocation_type, kTfLiteArenaRw);

    ASSERT_EQ(interpreter_->Invoke(), kTfLiteOk);
    TfLiteTensor* output1 = interpreter_->tensor(interpreter_->outputs()[0]);
    CheckIntTensor(output1, {1}, {i + 1});
    TfLiteTensor* output2 = interpreter_->tensor(interpreter_->outputs()[1]);
    CheckIntTensor(output2, {1}, {expected[i]});
  }
}

TEST_F(WhileTest, TestTriangularNumberSequenceWithShallowCopy) {
  const std::vector<int> expected = {1, 3, 6, 10, 15, 21, 28};
  for (int i = 0; i < expected.size(); ++i) {
    interpreter_ = std::make_unique<Interpreter>();
    AddSubgraphs(2);
    builder_->BuildLessEqualCondSubgraph(interpreter_->subgraph(1), i);
    builder_->BuildAccumulateLoopBodySubgraph(interpreter_->subgraph(2));
    builder_->BuildWhileSubgraph(&interpreter_->primary_subgraph());

    interpreter_->ResizeInputTensor(interpreter_->inputs()[0], {1});
    // Use 4MB inputs to test shallow copy.
    interpreter_->ResizeInputTensor(interpreter_->inputs()[1], {1000000});
    // Apply DynamicAllocationForLargeTensors option to enable shallow copy.
    InterpreterOptions options;
    options.OptimizeMemoryForLargeTensors(1000000);
    ASSERT_EQ(interpreter_->ApplyOptions(&options), kTfLiteOk);
    const size_t initial_mem_usage =
        profiling::memory::GetMemoryUsage().max_rss_kb;
    ASSERT_EQ(interpreter_->AllocateTensors(), kTfLiteOk);
    // Memory usage shouldn't exceed 9MB (2 x inputs + margin).
    ASSERT_LE(
        profiling::memory::GetMemoryUsage().max_rss_kb - initial_mem_usage,
        9000);
    FillIntTensor(interpreter_->tensor(interpreter_->inputs()[0]), {1});
    const std::vector<int> input_vector(1000000, 1);
    FillIntTensor(interpreter_->tensor(interpreter_->inputs()[1]),
                  input_vector);
    auto body_subgraph = interpreter_->subgraph(2);

    ASSERT_EQ(interpreter_->Invoke(), kTfLiteOk);

    // While BODY inputs are dynamic tensors with shallow copy.
    TfLiteTensor* subgraph_input2 =
        body_subgraph->tensor(body_subgraph->inputs()[1]);
    ASSERT_EQ(subgraph_input2->allocation_type, kTfLiteDynamic);

    TfLiteTensor* output1 = interpreter_->tensor(interpreter_->outputs()[0]);
    CheckIntTensor(output1, {1}, {i + 1});
    TfLiteTensor* output2 = interpreter_->tensor(interpreter_->outputs()[1]);
    const std::vector<int> expected2(1000000, expected[i]);
    CheckIntTensor(output2, {1000000}, expected2);
  }
}

TEST_F(WhileTest, TestPadLoop) {
  interpreter_ = std::make_unique<Interpreter>();
  AddSubgraphs(2);
  builder_->BuildLessEqualCondSubgraph(interpreter_->subgraph(1), 3);
  builder_->BuildPadLoopBodySubgraph(interpreter_->subgraph(2), {1, 2});
  builder_->BuildWhileSubgraph(&interpreter_->primary_subgraph());

  interpreter_->ResizeInputTensor(interpreter_->inputs()[0], {1});
  interpreter_->ResizeInputTensor(interpreter_->inputs()[1], {2});
  ASSERT_EQ(interpreter_->AllocateTensors(), kTfLiteOk);

  FillIntTensor(interpreter_->tensor(interpreter_->inputs()[0]), {1});
  FillIntTensor(interpreter_->tensor(interpreter_->inputs()[1]), {5, 7});

  ASSERT_EQ(interpreter_->Invoke(), kTfLiteOk);
  TfLiteTensor* output1 = interpreter_->tensor(interpreter_->outputs()[0]);
  CheckIntTensor(output1, {1}, {4});
  TfLiteTensor* output2 = interpreter_->tensor(interpreter_->outputs()[1]);
  CheckIntTensor(output2, {11}, {0, 0, 0, 5, 7, 0, 0, 0, 0, 0, 0});

  // The extra invocation serves as a regression test: There was a bug that
  // invoking a while loop with dynamic shaped body makes the interpreter
  // state uninvokable.
  ASSERT_EQ(interpreter_->Invoke(), kTfLiteOk);
}

TEST_F(WhileTest, TestPadLoopWithShallowCopy) {
  interpreter_ = std::make_unique<Interpreter>();
  AddSubgraphs(2);
  builder_->BuildLessEqualCondSubgraph(interpreter_->subgraph(1), 3);
  builder_->BuildPadLoopBodySubgraph(interpreter_->subgraph(2), {1, 2});
  builder_->BuildWhileSubgraph(&interpreter_->primary_subgraph());

  interpreter_->ResizeInputTensor(interpreter_->inputs()[0], {1});
  // Use 4MB inputs to test shallow copy.
  interpreter_->ResizeInputTensor(interpreter_->inputs()[1], {1000000});
  ASSERT_EQ(interpreter_->AllocateTensors(), kTfLiteOk);

  FillIntTensor(interpreter_->tensor(interpreter_->inputs()[0]), {1});
  std::vector<int> input_vector(1000000, 0);
  input_vector[0] = 5;
  input_vector[1] = 7;
  FillIntTensor(interpreter_->tensor(interpreter_->inputs()[1]), input_vector);

  ASSERT_EQ(interpreter_->Invoke(), kTfLiteOk);
  TfLiteTensor* output1 = interpreter_->tensor(interpreter_->outputs()[0]);
  CheckIntTensor(output1, {1}, {4});
  TfLiteTensor* output2 = interpreter_->tensor(interpreter_->outputs()[1]);
  std::vector<int> output_vector(1000009, 0);
  output_vector[3] = 5;
  output_vector[4] = 7;
  CheckIntTensor(output2, {1000009}, output_vector);

  // The extra invocation serves as a regression test: There was a bug that
  // invoking a while loop with dynamic shaped body makes the interpreter
  // state uninvokable.
  ASSERT_EQ(interpreter_->Invoke(), kTfLiteOk);
}

TEST_F(WhileTest, TestWhileLoopWithDynamicTensor) {
  interpreter_ = std::make_unique<Interpreter>();
  AddSubgraphs(2);
  builder_->BuildLessEqualCondSubgraphWithDynamicTensor(
      interpreter_->subgraph(1), 3);
  builder_->BuildBodySubgraphWithDynamicTensor(interpreter_->subgraph(2));
  builder_->BuildWhileSubgraphWithDynamicTensor(
      &interpreter_->primary_subgraph());

  interpreter_->ResizeInputTensor(interpreter_->inputs()[0], {});
  interpreter_->ResizeInputTensor(interpreter_->inputs()[1], {});
  interpreter_->ResizeInputTensor(interpreter_->inputs()[2], {1});
  ASSERT_EQ(interpreter_->AllocateTensors(), kTfLiteOk);

  FillScalarStringTensor(interpreter_->tensor(interpreter_->inputs()[0]), "A");
  FillScalarStringTensor(interpreter_->tensor(interpreter_->inputs()[1]), "A");
  FillIntTensor(interpreter_->tensor(interpreter_->inputs()[2]), {1});

  ASSERT_EQ(interpreter_->Invoke(), kTfLiteOk);
  TfLiteTensor* string_output1 =
      interpreter_->tensor(interpreter_->outputs()[0]);
  CheckScalarStringTensor(string_output1, "A");
  TfLiteTensor* string_output2 =
      interpreter_->tensor(interpreter_->outputs()[1]);
  CheckStringTensor(string_output2, {4}, {"A", "A", "A", "A"});
  TfLiteTensor* integer_output =
      interpreter_->tensor(interpreter_->outputs()[2]);
  CheckIntTensor(integer_output, {1}, {4});

  // The extra invocation serves as a regression test: There was a bug that
  // invoking a while loop with dynamic shaped body makes the interpreter
  // state uninvokable.
  ASSERT_EQ(interpreter_->Invoke(), kTfLiteOk);
}

}  // namespace
}  // namespace tflite
