// Copyright 2017 Google Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//    http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "cpu_instructions/x86/microarchitectures.h"

#include <vector>
#include "strings/string.h"

#include "cpu_instructions/base/cpu_model.h"
#include "cpu_instructions/proto/microarchitecture.pb.h"
#include "glog/logging.h"
#include "src/google/protobuf/text_format.h"
#include "strings/str_cat.h"

namespace cpu_instructions {
namespace x86 {
namespace {

// This is derived from Figure 2-1 "CPU Core Pipeline Functionality of the
// Skylake Microarchitecture" and Table 2-1. "Dispatch Port and Execution Stacks
// of the Skylake Microarchitecture" of the June 2016 edition of the Intel
// Optimization Reference Manual, Order Number 248966-033.
// http://www.intel.com/content/dam/www/public/us/en/documents/manuals/64-ia-32-architectures-optimization-manual.pdf
constexpr const char kSkylakeMicroarchitecture[] = R"(
    ports {
      comments: "Integer ALU"
      comments: "Integer Shift"
      comments: "Branch"
      comments: "Vector FMA"
      comments: "Vector Multiply"
      comments: "Vector Add"
      comments: "Vector ALU"
      comments: "Vector Shifts"
      comments: "Vector Divide"
    }
    ports {
      comments: "Integer ALU"
      comments: "Fast LEA"
      comments: "Integer Multiply"
      comments: "Vector FMA"
      comments: "Vector Multiply"
      comments: "Vector Add"
      comments: "Vector ALU"
      comments: "Vector Shifts"
      comments: "Slow LEA"
    }
    ports {
      comments: "Load & Store Address"
    }
    ports {
      comments: "Load & Store Address"
    }
    ports {
      comments: "Store Data"
    }
    ports {
      comments: "Integer ALU"
      comments: "Fast LEA"
      comments: "Vector Shuffle"
      comments: "Vector ALU"
      comments: "CVT"
    }
    ports {
      comments: "Integer ALU"
      comments: "Integer Shift"
      comments: "Branch"
    }
    ports {
      comments: "Store Address"
    }
    port_masks {
      # Divide: divp*, divs*, vdiv*, sqrt*, vsqrt*, rcp*, vrcp*, rsqrt*, idiv
      comment: "Divide, vector int multiply, vector shifts."
      port_numbers: 0
    }
    port_masks {
      # (v)mul*, (v)pmul*, (v)pmadd*,
      # (v)movsd/ss, (v)movd gpr,
      comment: "FMA, FP multiply, FP load, Vector Multiply"
      port_numbers: [0, 1]
    }
    port_masks {
      # (v)pand, (v)por, (v)pxor, (v)movq, (v)movq, (v)movap*, (v)movup*,
      # (v)andp*, (v)orp*, (v)paddb/w/d/q, (v)blendv*, (v)blendp*, (v)pblendd
      comment: "Vector ALU."
      port_numbers: [0, 1, 5]
    }
    port_masks {
      # add, and, cmp, or, test, xor, movzx, movsx, mov, (v)movdqu, (v)movdqa,
      # (v)movap*, (v)movup*
      comment: "Integer ALU."
      port_numbers: [0, 1, 5, 6]
    }
    port_masks {
      # Shifts: sal, shl, rol, adc, sarx, adcx, adox, etc.
      comment: "Jcc & fused arithmetic (predicted not taken). Integer shift."
      port_numbers: [0, 6]
    }
    port_masks {
      # mul, imul, bsr, rcl, shld, mulx, pdep, etc.
      comment: "Slow int, FP add. LEA (RIP or 3 components in address)."
      port_numbers: 1
    }
    port_masks {
      # (v)addp*, (v)cmpp*, (v)max*, (v)min*, (v)padds*, (v)paddus*, (v)psign,
      # (v)pabs, (v)pavgb, (v)pcmpeq*, (v)pmax, (v)cvtps2dq, (v)cvtdq2ps,
      # (v)cvtsd2si, (v)cvtss2s
      comment: "Vector int ALU. Integer LEA (2 components in address)."
      port_numbers: [1, 5]
    }
    port_masks {
      comment: "Load/store address generation."
      port_numbers: [2, 3]
    }
    port_masks {
      comment: "Store address generation."
      port_numbers: [2, 3, 7]
    }
    port_masks {
      comment: "Store data."
      port_numbers: 4
    }
    port_masks {
      # (v)shufp*, vperm*, (v)pack*, (v)unpck*, (v)punpck*, (v)pshuf*,
      # (v)pslldq, (v)alignr, (v)pmovzx*, vbroadcast*, (v)pslldq, (v)psrldq,
      # (v)pblendw
      comment: "Vector shuffle."
      port_numbers: 5
    }
    port_masks {
      comment: "Partial integer ALU (AAM, MUL, DIV). "
               "JMP, Jcc & fused arithmetic predicted taken."
      port_numbers: 6
    }
    protected_mode {
      protected_modes: [0, 1, 2]
    }
    load_store_address_generation_port_mask_index: 8
    store_address_generation_port_mask_index: 9
    store_data_port_mask_index: 10
    perf_events {
      # TODO(bdb): Only consider user-time measurements with the :u modifier.
      # NOTE(bdb): The events "uops_dispatched_port" (see
      # https://download.01.org/perfmon/SKL/Skylake_core_V24.json) are
      # incorrectly named "uops_dispatched" in libpfm.
      # TODO(bdb): Correct this when libpfm is corrected.
      computation_events: "uops_dispatched:port_0"
      computation_events: "uops_dispatched:port_1"
      computation_events: "uops_dispatched:port_5"
      computation_events: "uops_dispatched:port_6"
      memory_events: "uops_dispatched:port_2"
      memory_events: "uops_dispatched:port_3"
      memory_events: "uops_dispatched:port_4"
      memory_events: "uops_dispatched:port_7"
      cycle_events: "cycles"
      cycle_events: "instructions"
      cycle_events: "ild_stall.lcp"
      uops_events: "uops_issued:any"
      uops_events: "uops_retired:all"
    }
    )";

constexpr const char kSkylakeConsumerModels[] = R"(
    id: "skl"
    cpu_models {
      id: 'intel:06_4E'
    }
    cpu_models {
      id: 'intel:06_5E'
    }
    )";

constexpr const char kSkylakeXeonModels[] = R"(
    id: "skx"
    cpu_models {
      id: 'intel:06_55'
    }
    )";

// The Haswell CPU microarchitecture.
constexpr const char kHaswellMicroarchitecture[] = R"(
    ports {
      comments: "Integer ALU & Shift"
      comments: "FMA, 256-bit FP Multiply"
      comments: "Vector Int Multiply"
      comments: "Vector Logicals"
      comments: "Branch"
      comments: "Divide"
      comments: "Vector Shifts"
    }
    ports {
      comments: "Integer ALU & LEA"
      comments: "FMA, FP Multiply, 256-bit FP Add"
      comments: "Vector Int ALU"
      comments: "Vector Logicals"
    }
    ports {
      comments: "Load & Store Address"
    }
    ports {
      comments: "Load & Store Address"
    }
    ports {
      comments: "Store Data"
    }
    ports {
      comments: "Integer ALU & LEA"
      comments: "Vector Shuffle"
      comments: "Vector Int ALU"
      comments: "256-bit Vector Logicals"
    }
    ports {
      comments: "Integer ALU & Shift"
      comments: "Branch"
    }
    ports {
      comments: "Store Address"
    }
    port_masks {
      comment: "Divide, vector shifts, vector int multiply, vector shifts."
      port_numbers: 0
    }
    port_masks {
      comment: "FMA, FP multiply, FP load."
      port_numbers: [0, 1]
    }
    port_masks {
      comment: "Vector logicals."
      port_numbers: [0, 1, 5]
    }
    port_masks {
      comment: "Integer ALU."
      port_numbers: [0, 1, 5, 6]
    }
    port_masks {
      comment: "Jcc & fused arithmetic (predicted not taken). Integer shift."
      port_numbers: [0, 6]
    }
    port_masks {
      comment: "FP add. LEA (RIP or 3 components in address)."
      port_numbers: 1
    }
    port_masks {
      comment: "Vector int ALU. Integer LEA (2 components in address)."
      port_numbers: [1, 5]
    }
    port_masks {
      comment: "Load/store address generation."
      port_numbers: [2, 3]
    }
    port_masks {
      comment: "Store address generation."
      port_numbers: [2, 3, 7]
    }
    port_masks {
      comment: "Store data."
      port_numbers: 4
    }
    port_masks {
      comment: "Vector shuffle."
      port_numbers: 5
    }
    port_masks {
      comment: "Partial integer ALU (AAM, MUL, DIV). JMP, Jcc & fused arithmetic predicted taken."
      port_numbers: 6
    }
    protected_mode {
      protected_modes: [0, 1, 2]
    }
    load_store_address_generation_port_mask_index: 8
    store_address_generation_port_mask_index: 9
    store_data_port_mask_index: 10
    perf_events {
      # TODO(bdb): Only consider user-time measurements with the :u modifier.
      computation_events: "uops_executed_port:port_0"
      computation_events: "uops_executed_port:port_1"
      computation_events: "uops_executed_port:port_5"
      computation_events: "uops_executed_port:port_6"
      memory_events: "uops_executed_port:port_2"
      memory_events: "uops_executed_port:port_3"
      memory_events: "uops_executed_port:port_4"
      memory_events: "uops_executed_port:port_7"
      cycle_events: "cycles"
      cycle_events: "instructions"
      cycle_events: "ild_stall.lcp"
      uops_events: "uops_issued:any"
      uops_events: "uops_retired:all"
    }

    num_simple_instructions_decoded_per_cycle: 3
    num_complex_instructions_decoded_per_cycle: 1
    reorder_buffer_size_in_uops: 192
    reservation_station_size_in_uops: 60
    num_execution_ports: 8
    )";

constexpr const char kHaswellModels[] = R"(
    id: "hsw"
    cpu_models {
      id: 'intel:06_3C'
    }
    cpu_models {
      id: 'intel:06_3F'
    }
    cpu_models {
      id: 'intel:06_45'
    }
    cpu_models {
      id: 'intel:06_46'
    }
    )";

constexpr const char kBroadwellModels[] = R"(
    id: "bdw"
    cpu_models {
      id: 'intel:06_3D'
    }
    cpu_models {
      id: 'intel:06_47'
    }
    cpu_models {
      id: 'intel:06_56'
    }
    )";

constexpr const char kSandyBridgeMicroarchitecture[] = R"(
    ports {
      comments: "Integer ALU"
      comments: "Shift"
      comments: "256-bit FP Multiply"
      comments: "Vector Int Multiply"
      comments: "Vector Logicals"
      comments: "Vector Shifts"
      comments: "Divide"
    }
    ports {
      comments: "Integer ALU & LEA"
      comments: "256-bit FP Add"
      comments: "Vector Int ALU"
      comments: "Vector Logicals"
    }
    ports {
      comments: "Load/Store Address"
    }
    ports {
      comments: "Load/Store Address"
    }
    ports {
      comments: "Store Data"
    }
    ports {
      comments: "Integer ALU"
      comments: "Shift"
      comments: "Vector Int ALU"
      comments: "256-bit Vector Logicals"
      comments: "Branch"
    }
    port_masks {
      comment: "Divide, vector shifts, vector int multiply, vector shifts, "
               "FP multiply, Jcc & fused arithmetic, JMP."
      port_numbers: 0
    }
    port_masks {
      comment: "Vector logicals, Integer ALU."
      port_numbers: [0, 1, 5]
    }
    port_masks {
      comment: "FP add. LEA (RIP or 3 components in address)."
      port_numbers: 1
    }
    port_masks {
      comment: "Vector int ALU. Integer LEA (2 components in address)."
      port_numbers: [1, 5]
    }
    port_masks {
      comment: "Load/store address generation."
      port_numbers: [2, 3]
    }
    port_masks {
      comment: "Store data."
      port_numbers: 4
    }
    protected_mode {
      protected_modes: [0, 1, 2]
    }
    load_store_address_generation_port_mask_index: 5
    store_address_generation_port_mask_index: 5
    store_data_port_mask_index: 6
    perf_events {
      # TODO(bdb): Only consider user-time measurements with the :u modifier.
      computation_events: "uops_dispatched_port:port_0"
      computation_events: "uops_dispatched_port:port_1"
      computation_events: "uops_dispatched_port:port_5"
      memory_events: "uops_dispatched_port:port_2"
      memory_events: "uops_dispatched_port:port_3"
      memory_events: "uops_dispatched_port:port_4"
      cycle_events: "cycles"
      cycle_events: "instructions"
      cycle_events: "ild_stall.lcp"
      uops_events: "uops_issued:any"
      uops_events: "uops_retired:all"
    }
    )";

constexpr const char kIvyBridgeModels[] = R"(
    id: "ivb"
    cpu_models {
      id: 'intel:06_3A'
    }
    cpu_models {
      id: 'intel:06_3E'
    }
    )";

constexpr const char kSandyBridgeModels[] = R"(
    id: "snb"
    cpu_models {
      id: 'intel:06_2A'
    }
    cpu_models {
      id: 'intel:06_2D'
    }
    )";

constexpr const char kNehalemMicroarchitecture[] = R"(
    ports {
      comments: "Integer ALU"
      comments: "Shift"
      comments: "FP Multiply"
      comments: "Vector Int Multiply"
      comments: "Vector Logicals"
      comments: "Vector Shifts"
      comments: "Divide"
    }
    ports {
      comments: "Integer ALU & LEA"
      comments: "FP Add"
      comments: "Vector Int ALU"
      comments: "Vector Logicals"
    }
    ports {
      comments: "Load"
    }
    ports {
      comments: "Store Address"
    }
    ports {
      comments: "Store Data"
    }
    ports {
      comments: "Integer ALU"
      comments: "Shift"
      comments: "Vector Int ALU"
      comments: "Vector Logicals"
      comments: "Branch"
    }
    port_masks {
      comment: "Divide, vector shifts, vector int multiply, vector shifts, "
               "FP multiply, Jcc & fused arithmetic, JMP."
      port_numbers: 0
    }
    port_masks {
      comment: "Vector logicals, Integer ALU."
      port_numbers: [0, 1, 5]
    }
    port_masks {
      comment: "FP add. LEA (RIP or 3 components in address)."
      port_numbers: 1
    }
    port_masks {
      comment: "Vector int ALU. Integer LEA (2 components in address)."
      port_numbers: [1, 5]
    }
    port_masks {
      comment: "Load."
      port_numbers: 2
    }
    port_masks {
      comment: "Store address generation."
      port_numbers: 3
    }
    port_masks {
      comment: "Store data."
      port_numbers: 4
    }
    protected_mode {
      protected_modes: [0, 1, 2]
    }
    load_store_address_generation_port_mask_index: 5
    store_address_generation_port_mask_index: 6
    store_data_port_mask_index: 7
    perf_events {
      # TODO(bdb): Only consider user-time measurements with the :u modifier.
      computation_events: "uops_executed:port0"
      computation_events: "uops_executed:port1"
      computation_events: "uops_executed:port5"
      computation_events: "uops_executed:port015"  # WTF ?
      memory_events: "uops_executed:port2"
      memory_events: "uops_executed:port3"
      memory_events: "uops_executed:port4"
      cycle_events: "cycles"
      cycle_events: "instructions"
      cycle_events: "ild_stall.lcp"
      uops_events: "uops_issued"
      uops_events: "uops_retired"
    }
    )";

constexpr const char kWestmireModels[] = R"(
    id: "wsm"
    cpu_models {
      id: 'intel:06_25'
    }
    cpu_models {
      id: 'intel:06_2C'
    }
    cpu_models {
      id: 'intel:06_2F'
    }
    )";

constexpr const char kNehalemModels[] = R"(
    id: "nhm"
    cpu_models {
      id: 'intel:06_1A'
    }
    cpu_models {
      id: 'intel:06_1E'
    }
    cpu_models {
      id: 'intel:06_1F'
    }
    cpu_models {
      id: 'intel:06_2E'
    }
    )";

constexpr const char kEnhancedCoreModels[] = R"(
    id: "enhanced_core"
    cpu_models {
      id: 'intel:06_17'
    }
    cpu_models {
      id: 'intel:06_1D'
    }
    )";

constexpr const char kCoreModels[] = R"(
    id: "core"
    cpu_models {
      id: 'intel:06_0F'
    }
    )";

const MicroArchitecturesProto& GetMicroArchitecturesProto() {
  static const MicroArchitecturesProto* const microarchitectures = []() {
    const std::vector<string> sources = {
        StrCat(kSkylakeConsumerModels, kSkylakeMicroarchitecture),
        StrCat(kSkylakeXeonModels, kSkylakeMicroarchitecture),
        StrCat(kHaswellModels, kHaswellMicroarchitecture),
        StrCat(kBroadwellModels, kHaswellMicroarchitecture),
        StrCat(kIvyBridgeModels, kSandyBridgeMicroarchitecture),
        StrCat(kSandyBridgeModels, kSandyBridgeMicroarchitecture),
        StrCat(kWestmireModels, kNehalemMicroarchitecture),
        StrCat(kNehalemModels, kNehalemMicroarchitecture),
        // NOTE(bdb): As of 2017-03-01 we do not need the itineraries of the
        // Core and Enhanced Core architectures.
        kEnhancedCoreModels, kCoreModels};
    auto* const result = new MicroArchitecturesProto();
    for (const string& source : sources) {
      CHECK(::google::protobuf::TextFormat::ParseFromString(
          source, result->add_microarchitectures()));
    }
    return result;
  }();
  return *microarchitectures;
}
REGISTER_MICRO_ARCHITECTURES(GetMicroArchitecturesProto);

}  // namespace
}  // namespace x86
}  // namespace cpu_instructions
