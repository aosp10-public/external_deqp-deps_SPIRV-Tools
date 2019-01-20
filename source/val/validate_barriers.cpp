// Copyright (c) 2018 Google LLC.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

// Validates correctness of barrier SPIR-V instructions.

#include "source/val/validate.h"

#include <string>

#include "source/diagnostic.h"
#include "source/opcode.h"
#include "source/spirv_constant.h"
#include "source/spirv_target_env.h"
#include "source/util/bitutils.h"
#include "source/val/instruction.h"
#include "source/val/validate_scopes.h"
#include "source/val/validation_state.h"

namespace spvtools {
namespace val {
namespace {

// Validates Memory Semantics operand.
spv_result_t ValidateMemorySemantics(ValidationState_t& _,
                                     const Instruction* inst, uint32_t id) {
  const SpvOp opcode = inst->opcode();
  bool is_int32 = false, is_const_int32 = false;
  uint32_t value = 0;
  std::tie(is_int32, is_const_int32, value) = _.EvalInt32IfConst(id);

  if (!is_int32) {
    return _.diag(SPV_ERROR_INVALID_DATA, inst)
           << spvOpcodeString(opcode)
           << ": expected Memory Semantics to be a 32-bit int";
  }

  if (!is_const_int32) {
    if (_.HasCapability(SpvCapabilityShader)) {
      return _.diag(SPV_ERROR_INVALID_DATA, inst)
             << "Memory Semantics ids must be OpConstant when Shader "
                "capability is present";
    }
    return SPV_SUCCESS;
  }

  if (_.memory_model() == SpvMemoryModelVulkanKHR &&
      value & SpvMemorySemanticsSequentiallyConsistentMask) {
    return _.diag(SPV_ERROR_INVALID_DATA, inst)
           << "SequentiallyConsistent memory "
              "semantics cannot be used with "
              "the VulkanKHR memory model.";
  }

  if (value & SpvMemorySemanticsOutputMemoryKHRMask &&
      !_.HasCapability(SpvCapabilityVulkanMemoryModelKHR)) {
    return _.diag(SPV_ERROR_INVALID_DATA, inst)
           << spvOpcodeString(opcode)
           << ": Memory Semantics OutputMemoryKHR requires capability "
           << "VulkanMemoryModelKHR";
  }

  if (value & SpvMemorySemanticsMakeAvailableKHRMask &&
      !_.HasCapability(SpvCapabilityVulkanMemoryModelKHR)) {
    return _.diag(SPV_ERROR_INVALID_DATA, inst)
           << spvOpcodeString(opcode)
           << ": Memory Semantics MakeAvailableKHR requires capability "
           << "VulkanMemoryModelKHR";
  }

  if (value & SpvMemorySemanticsMakeVisibleKHRMask &&
      !_.HasCapability(SpvCapabilityVulkanMemoryModelKHR)) {
    return _.diag(SPV_ERROR_INVALID_DATA, inst)
           << spvOpcodeString(opcode)
           << ": Memory Semantics MakeVisibleKHR requires capability "
           << "VulkanMemoryModelKHR";
  }

  const size_t num_memory_order_set_bits = spvtools::utils::CountSetBits(
      value & (SpvMemorySemanticsAcquireMask | SpvMemorySemanticsReleaseMask |
               SpvMemorySemanticsAcquireReleaseMask |
               SpvMemorySemanticsSequentiallyConsistentMask));

  if (num_memory_order_set_bits > 1) {
    return _.diag(SPV_ERROR_INVALID_DATA, inst)
           << spvOpcodeString(opcode)
           << ": Memory Semantics can have at most one of the following bits "
              "set: Acquire, Release, AcquireRelease or SequentiallyConsistent";
  }

  if (value & SpvMemorySemanticsMakeAvailableKHRMask &&
      !(value & (SpvMemorySemanticsReleaseMask |
                 SpvMemorySemanticsAcquireReleaseMask))) {
    return _.diag(SPV_ERROR_INVALID_DATA, inst)
           << spvOpcodeString(opcode)
           << ": MakeAvailableKHR Memory Semantics also requires either "
              "Release or AcquireRelease Memory Semantics";
  }

  if (value & SpvMemorySemanticsMakeVisibleKHRMask &&
      !(value & (SpvMemorySemanticsAcquireMask |
                 SpvMemorySemanticsAcquireReleaseMask))) {
    return _.diag(SPV_ERROR_INVALID_DATA, inst)
           << spvOpcodeString(opcode)
           << ": MakeVisibleKHR Memory Semantics also requires either Acquire "
              "or AcquireRelease Memory Semantics";
  }

  if (spvIsVulkanEnv(_.context()->target_env)) {
    const bool includes_storage_class =
        value & (SpvMemorySemanticsUniformMemoryMask |
                 SpvMemorySemanticsWorkgroupMemoryMask |
                 SpvMemorySemanticsImageMemoryMask |
                 SpvMemorySemanticsOutputMemoryKHRMask);

    if (opcode == SpvOpMemoryBarrier && !num_memory_order_set_bits) {
      return _.diag(SPV_ERROR_INVALID_DATA, inst)
             << spvOpcodeString(opcode)
             << ": Vulkan specification requires Memory Semantics to have one "
                "of the following bits set: Acquire, Release, AcquireRelease "
                "or SequentiallyConsistent";
    }

    if (opcode == SpvOpMemoryBarrier && !includes_storage_class) {
      return _.diag(SPV_ERROR_INVALID_DATA, inst)
             << spvOpcodeString(opcode)
             << ": expected Memory Semantics to include a Vulkan-supported "
                "storage class";
    }

#if 0
    // TODO(atgoo@github.com): this check fails Vulkan CTS, reenable once fixed.
    if (opcode == SpvOpControlBarrier && value && !includes_storage_class) {
      return _.diag(SPV_ERROR_INVALID_DATA, inst)
             << spvOpcodeString(opcode)
             << ": expected Memory Semantics to include a Vulkan-supported "
                "storage class if Memory Semantics is not None";
    }
#endif
  }

  if (value & (SpvMemorySemanticsMakeAvailableKHRMask |
               SpvMemorySemanticsMakeVisibleKHRMask)) {
    const bool includes_storage_class =
        value & (SpvMemorySemanticsUniformMemoryMask |
                 SpvMemorySemanticsSubgroupMemoryMask |
                 SpvMemorySemanticsWorkgroupMemoryMask |
                 SpvMemorySemanticsCrossWorkgroupMemoryMask |
                 SpvMemorySemanticsAtomicCounterMemoryMask |
                 SpvMemorySemanticsImageMemoryMask |
                 SpvMemorySemanticsOutputMemoryKHRMask);

    if (!includes_storage_class) {
      return _.diag(SPV_ERROR_INVALID_DATA, inst)
             << spvOpcodeString(opcode)
             << ": expected Memory Semantics to include a storage class";
    }
  }

  // TODO(atgoo@github.com) Add checks for OpenCL and OpenGL environments.

  return SPV_SUCCESS;
}

}  // namespace

// Validates correctness of barrier instructions.
spv_result_t BarriersPass(ValidationState_t& _, const Instruction* inst) {
  const SpvOp opcode = inst->opcode();
  const uint32_t result_type = inst->type_id();

  switch (opcode) {
    case SpvOpControlBarrier: {
      if (spvVersionForTargetEnv(_.context()->target_env) <
          SPV_SPIRV_VERSION_WORD(1, 3)) {
        _.function(inst->function()->id())
            ->RegisterExecutionModelLimitation(
                [](SpvExecutionModel model, std::string* message) {
                  if (model != SpvExecutionModelTessellationControl &&
                      model != SpvExecutionModelGLCompute &&
                      model != SpvExecutionModelKernel &&
                      model != SpvExecutionModelTaskNV &&
                      model != SpvExecutionModelMeshNV) {
                    if (message) {
                      *message =
                          "OpControlBarrier requires one of the following "
                          "Execution "
                          "Models: TessellationControl, GLCompute or Kernel";
                    }
                    return false;
                  }
                  return true;
                });
      }

      const uint32_t execution_scope = inst->word(1);
      const uint32_t memory_scope = inst->word(2);
      const uint32_t memory_semantics = inst->word(3);

      if (auto error = ValidateExecutionScope(_, inst, execution_scope)) {
        return error;
      }

      if (auto error = ValidateMemoryScope(_, inst, memory_scope)) {
        return error;
      }

      if (auto error = ValidateMemorySemantics(_, inst, memory_semantics)) {
        return error;
      }
      break;
    }

    case SpvOpMemoryBarrier: {
      const uint32_t memory_scope = inst->word(1);
      const uint32_t memory_semantics = inst->word(2);

      if (auto error = ValidateMemoryScope(_, inst, memory_scope)) {
        return error;
      }

      if (auto error = ValidateMemorySemantics(_, inst, memory_semantics)) {
        return error;
      }
      break;
    }

    case SpvOpNamedBarrierInitialize: {
      if (_.GetIdOpcode(result_type) != SpvOpTypeNamedBarrier) {
        return _.diag(SPV_ERROR_INVALID_DATA, inst)
               << spvOpcodeString(opcode)
               << ": expected Result Type to be OpTypeNamedBarrier";
      }

      const uint32_t subgroup_count_type = _.GetOperandTypeId(inst, 2);
      if (!_.IsIntScalarType(subgroup_count_type) ||
          _.GetBitWidth(subgroup_count_type) != 32) {
        return _.diag(SPV_ERROR_INVALID_DATA, inst)
               << spvOpcodeString(opcode)
               << ": expected Subgroup Count to be a 32-bit int";
      }
      break;
    }

    case SpvOpMemoryNamedBarrier: {
      const uint32_t named_barrier_type = _.GetOperandTypeId(inst, 0);
      if (_.GetIdOpcode(named_barrier_type) != SpvOpTypeNamedBarrier) {
        return _.diag(SPV_ERROR_INVALID_DATA, inst)
               << spvOpcodeString(opcode)
               << ": expected Named Barrier to be of type OpTypeNamedBarrier";
      }

      const uint32_t memory_scope = inst->word(2);
      const uint32_t memory_semantics = inst->word(3);

      if (auto error = ValidateMemoryScope(_, inst, memory_scope)) {
        return error;
      }

      if (auto error = ValidateMemorySemantics(_, inst, memory_semantics)) {
        return error;
      }
      break;
    }

    default:
      break;
  }

  return SPV_SUCCESS;
}

}  // namespace val
}  // namespace spvtools