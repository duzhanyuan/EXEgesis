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

#include "cpu_instructions/x86/operand_translator.h"

#include <unordered_map>
#include "strings/string.h"

#include "base/stringprintf.h"
#include "glog/logging.h"
#include "strings/str_cat.h"
#include "util/gtl/map_util.h"
#include "util/task/canonical_errors.h"

namespace cpu_instructions {
namespace x86 {

namespace {

#define LABEL_OPERAND(x) "Label\n.rept " #x "\nNOP\n.endr\nLabel: NOP"
// NOTE(ondrasej): Using indirect addressing by a register is preferable here.
// When we use only a displacement, the compiler sometimes has a choice between
// one encoding based on ModR/M and one based on immediate values, and it
// usually picks the bad one. In case of CALL, it picks one that does not even
// compile and crashes LLVM on an assertion.
#define ADDRESS " ptr[RSI]"
#define OFFSET_ADDRESS " ptr DS:[RSI]"

// Returns an example of operand value for a given operand specification,
// e.g. '0x7e' for 'imm8', or 'xmm5' for 'xmm'.
string TranslateOperand(const string& operand) {
  static const std::unordered_map<string, string>* const kOperandTranslation =
      new std::unordered_map<string, string>({
          {"CR0-CR7", "CR0"},
          {"DR0-DR7", "DR0"},
          {"<XMM0>", ""},
          {"ST(i)", "ST(2)"},
          {"bnd", "bnd2"},
          // All rel*, m, and mem are fishy.
          {"imm8", "0x7e"},
          {"imm16", "0x7ffe"},
          {"imm32", "0x7ffffffe"},
          {"imm64", "0x400000000002d06d"},
          {"rel8", LABEL_OPERAND(64)},
          {"rel16", LABEL_OPERAND(0x100)},
          {"rel32", LABEL_OPERAND(0x10000)},
          {"m8", "byte" ADDRESS},
          {"mib", "qword" ADDRESS},
          {"moffs8", "byte" OFFSET_ADDRESS},
          {"m", "word" ADDRESS},
          {"m16", "word" ADDRESS},
          {"m16&16", "word" ADDRESS},
          {"m16&64", "qword" ADDRESS},
          {"m16int", "word" ADDRESS},
          {"moffs16", "word" OFFSET_ADDRESS},
          {"m2byte", "word" ADDRESS},
          {"m14byte", "dword" ADDRESS},  // LLVM differs from the Intel spec.
          {"m28byte", "dword" ADDRESS},  // LLVM differs from the Intel spec.
          {"m32", "dword" ADDRESS},
          {"m32&32", "dword" ADDRESS},
          {"moffs32", "dword" OFFSET_ADDRESS},
          {"m32fp", "dword" ADDRESS},
          {"m32int", "dword" ADDRESS},
          {"m64", "qword" ADDRESS},
          {"moffs64", "qword" OFFSET_ADDRESS},
          {"mem", "xmmword" ADDRESS},
          {"m64fp", "qword" ADDRESS},
          {"m64int", "dword" ADDRESS},
          {"m80dec", "xword" ADDRESS},
          {"m80bcd", "xword" ADDRESS},
          {"m80fp", "xword" ADDRESS},
          {"m128", "xmmword" ADDRESS},
          {"m256", "ymmword" ADDRESS},
          {"m512", "ymmword" ADDRESS},
          {"m94byte", "dword" ADDRESS},   // LLVM differs from the Intel spec.
          {"m108byte", "dword" ADDRESS},  // LLVM differs from the Intel spec.
          {"m512byte", "opaque" ADDRESS},
          {"ptr16:16", "0x7f16:0x7f16"},
          {"ptr16:32", "0x3039:0x30393039"},
          {"m16:16", "word" ADDRESS},
          {"m16:32", "dword" ADDRESS},
          {"m16:64", "qword" ADDRESS},
          {"xmm", "xmm5"},
          {"mm", "mm6"},
          {"Sreg", "cs"},
          {"ST(i)", "ST(3)"},
          {"vm32x", "[rsp + 4* xmm9]"},
          {"vm32y", "[rsp + 4* ymm10]"},
          {"vm64x", "[rsp + 8* xmm11]"},
          {"vm64y", "[rsp + 8* ymm12]"},
      });
  return FindWithDefault(*kOperandTranslation, operand, operand);
}

#undef LABEL_OPERAND
#undef ADDRESS
#undef OFFSET_ADDRESS

string TranslateGPR(const string& operand) {
  // Note: keep in sync with clobbered registers in AddItineraries.
  static const std::unordered_map<string, string>* const kGPRLegacyTranslation =
      new std::unordered_map<string, string>({
          {"r8", "ch"},
          {"r16", "cx"},
          {"r32", "ecx"},
          {"r32a", "eax"},
          {"r32b", "ebx"},
          {"r64", "rcx"},
          {"r64a", "rax"},
          {"r64b", "rbx"},
          // Warning: valid for r64 and r32
          {"reg", "rdx"},
      });

  return FindWithDefault(*kGPRLegacyTranslation, operand, operand);
}

string TranslateREX(const string& operand) {
  // Note: keep in sync with clobbered registers in AddItineraries.
  static const std::unordered_map<string, string>* const kGPRREXTranslation =
      new std::unordered_map<string, string>({
          {"r8", "r8b"},
          {"r16", "r10w"},
          {"r32", "r10d"},
          {"r32a", "r8d"},
          {"r32b", "r9d"},
          {"r64", "r10"},
          {"r64a", "r8"},
          {"r64b", "r9"},
          // Warning: valid for r64 and r32
          {"reg", "r11"},
      });
  return FindWithDefault(*kGPRREXTranslation, operand, operand);
}

}  // namespace

InstructionFormat InstantiateOperands(const InstructionProto& instruction) {
  InstructionFormat result;
  // Deal with the fact that the LLVM assembler cannot assemble MOV r64,imm64.
  const InstructionFormat& vendor_syntax = instruction.vendor_syntax();
  const bool is_movabs = vendor_syntax.mnemonic() == "MOV" &&
                         vendor_syntax.operands(1).name() == "imm64";
  result.set_mnemonic(is_movabs ? "MOVABS" : vendor_syntax.mnemonic());
  for (const auto& operand : vendor_syntax.operands()) {
    string code_operand = TranslateOperand(operand.name());
    if (code_operand == operand.name()) {
      code_operand = instruction.legacy_instruction()
                         ? TranslateGPR(operand.name())
                         : TranslateREX(operand.name());
    }
    if (!code_operand.empty()) {
      result.add_operands()->set_name(code_operand);
    } else {
      CHECK_EQ(operand.name(), "<XMM0>")
          << operand.name() << " could not be translated.";
    }
  }
  return result;
}

}  // namespace x86
}  // namespace cpu_instructions
