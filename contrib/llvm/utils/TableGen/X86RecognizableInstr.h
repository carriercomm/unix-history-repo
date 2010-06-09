//===- X86RecognizableInstr.h - Disassembler instruction spec ----*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file is part of the X86 Disassembler Emitter.
// It contains the interface of a single recognizable instruction.
// Documentation for the disassembler emitter in general can be found in
//  X86DisasemblerEmitter.h.
//
//===----------------------------------------------------------------------===//

#ifndef X86RECOGNIZABLEINSTR_H
#define X86RECOGNIZABLEINSTR_H

#include "X86DisassemblerTables.h"

#include "CodeGenTarget.h"
#include "Record.h"

#include "llvm/System/DataTypes.h"
#include "llvm/ADT/SmallVector.h"

namespace llvm {

namespace X86Disassembler {

/// RecognizableInstr - Encapsulates all information required to decode a single
///   instruction, as extracted from the LLVM instruction tables.  Has methods
///   to interpret the information available in the LLVM tables, and to emit the
///   instruction into DisassemblerTables.
class RecognizableInstr {
private:
  /// The opcode of the instruction, as used in an MCInst
  InstrUID UID;
  /// The record from the .td files corresponding to this instruction
  const Record* Rec;
  /// The prefix field from the record
  uint8_t Prefix;
  /// The opcode field from the record; this is the opcode used in the Intel
  /// encoding and therefore distinct from the UID
  uint8_t Opcode;
  /// The form field from the record
  uint8_t Form;
  /// The segment override field from the record
  uint8_t SegOvr;
  /// The hasOpSizePrefix field from the record
  bool HasOpSizePrefix;
  /// The hasREX_WPrefix field from the record
  bool HasREX_WPrefix;
  /// The hasLockPrefix field from the record
  bool HasLockPrefix;
  /// The isCodeGenOnly filed from the record
  bool IsCodeGenOnly;
  
  /// The instruction name as listed in the tables
  std::string Name;
  /// The AT&T AsmString for the instruction
  std::string AsmString;
  
  /// Indicates whether the instruction is SSE
  bool IsSSE;
  /// Indicates whether the instruction has FR operands - MOVs with FR operands
  /// are typically ignored
  bool HasFROperands;
  /// Indicates whether the instruction should be emitted into the decode
  /// tables; regardless, it will be emitted into the instruction info table
  bool ShouldBeEmitted;
  
  /// The operands of the instruction, as listed in the CodeGenInstruction.
  /// They are not one-to-one with operands listed in the MCInst; for example,
  /// memory operands expand to 5 operands in the MCInst
  const std::vector<CodeGenInstruction::OperandInfo>* Operands;
  /// The description of the instruction that is emitted into the instruction
  /// info table
  InstructionSpecifier* Spec;

  /// insnContext - Returns the primary context in which the instruction is
  ///   valid.
  ///
  /// @return - The context in which the instruction is valid.
  InstructionContext insnContext() const;
  
  enum filter_ret {
    FILTER_STRONG,    // instruction has no place in the instruction tables
    FILTER_WEAK,      // instruction may conflict, and should be eliminated if
                      // it does
    FILTER_NORMAL     // instruction should have high priority and generate an
                      // error if it conflcits with any other FILTER_NORMAL
                      // instruction
  };
  
  /// filter - Determines whether the instruction should be decodable.  Some 
  ///   instructions are pure intrinsics and use unencodable operands; many
  ///   synthetic instructions are duplicates of other instructions; other
  ///   instructions only differ in the logical way in which they are used, and
  ///   have the same decoding.  Because these would cause decode conflicts,
  ///   they must be filtered out.
  ///
  /// @return - The degree of filtering to be applied (see filter_ret).
  filter_ret filter() const;
  
  /// typeFromString - Translates an operand type from the string provided in
  ///   the LLVM tables to an OperandType for use in the operand specifier.
  ///
  /// @param s              - The string, as extracted by calling Rec->getName()
  ///                         on a CodeGenInstruction::OperandInfo.
  /// @param isSSE          - Indicates whether the instruction is an SSE 
  ///                         instruction.  For SSE instructions, immediates are 
  ///                         fixed-size rather than being affected by the
  ///                         mandatory OpSize prefix.
  /// @param hasREX_WPrefix - Indicates whether the instruction has a REX.W
  ///                         prefix.  If it does, 32-bit register operands stay
  ///                         32-bit regardless of the operand size.
  /// @param hasOpSizePrefix- Indicates whether the instruction has an OpSize
  ///                         prefix.  If it does not, then 16-bit register
  ///                         operands stay 16-bit.
  /// @return               - The operand's type.
  static OperandType typeFromString(const std::string& s, 
                                    bool isSSE,
                                    bool hasREX_WPrefix,
                                    bool hasOpSizePrefix);
  
  /// immediateEncodingFromString - Translates an immediate encoding from the
  ///   string provided in the LLVM tables to an OperandEncoding for use in
  ///   the operand specifier.
  ///
  /// @param s                - See typeFromString().
  /// @param hasOpSizePrefix  - Indicates whether the instruction has an OpSize
  ///                           prefix.  If it does not, then 16-bit immediate
  ///                           operands stay 16-bit.
  /// @return                 - The operand's encoding.
  static OperandEncoding immediateEncodingFromString(const std::string &s,
                                                     bool hasOpSizePrefix);
  
  /// rmRegisterEncodingFromString - Like immediateEncodingFromString, but
  ///   handles operands that are in the REG field of the ModR/M byte.
  static OperandEncoding rmRegisterEncodingFromString(const std::string &s,
                                                      bool hasOpSizePrefix);
  
  /// rmRegisterEncodingFromString - Like immediateEncodingFromString, but
  ///   handles operands that are in the REG field of the ModR/M byte.
  static OperandEncoding roRegisterEncodingFromString(const std::string &s,
                                                      bool hasOpSizePrefix);
  static OperandEncoding memoryEncodingFromString(const std::string &s,
                                                  bool hasOpSizePrefix);
  static OperandEncoding relocationEncodingFromString(const std::string &s,
                                                      bool hasOpSizePrefix);
  static OperandEncoding opcodeModifierEncodingFromString(const std::string &s,
                                                          bool hasOpSizePrefix);
  
  /// handleOperand - Converts a single operand from the LLVM table format to
  ///   the emitted table format, handling any duplicate operands it encounters
  ///   and then one non-duplicate.
  ///
  /// @param optional             - Determines whether to assert that the
  ///                               operand exists.
  /// @param operandIndex         - The index into the generated operand table.
  ///                               Incremented by this function one or more
  ///                               times to reflect possible duplicate 
  ///                               operands).
  /// @param physicalOperandIndex - The index of the current operand into the
  ///                               set of non-duplicate ('physical') operands.
  ///                               Incremented by this function once.
  /// @param numPhysicalOperands  - The number of non-duplicate operands in the
  ///                               instructions.
  /// @param operandMapping       - The operand mapping, which has an entry for
  ///                               each operand that indicates whether it is a
  ///                               duplicate, and of what.
  void handleOperand(bool optional,
                     unsigned &operandIndex,
                     unsigned &physicalOperandIndex,
                     unsigned &numPhysicalOperands,
                     unsigned *operandMapping,
                     OperandEncoding (*encodingFromString)
                       (const std::string&,
                        bool hasOpSizePrefix));
  
  /// shouldBeEmitted - Returns the shouldBeEmitted field.  Although filter()
  ///   filters out many instructions, at various points in decoding we
  ///   determine that the instruction should not actually be decodable.  In
  ///   particular, MMX MOV instructions aren't emitted, but they're only
  ///   identified during operand parsing.
  ///
  /// @return - true if at this point we believe the instruction should be
  ///   emitted; false if not.  This will return false if filter() returns false
  ///   once emitInstructionSpecifier() has been called.
  bool shouldBeEmitted() const {
    return ShouldBeEmitted;
  }
  
  /// emitInstructionSpecifier - Loads the instruction specifier for the current
  ///   instruction into a DisassemblerTables.
  ///
  /// @arg tables - The DisassemblerTables to populate with the specifier for
  ///               the current instruction.
  void emitInstructionSpecifier(DisassemblerTables &tables);
  
  /// emitDecodePath - Populates the proper fields in the decode tables
  ///   corresponding to the decode paths for this instruction.
  ///
  /// @arg tables - The DisassemblerTables to populate with the decode
  ///               decode information for the current instruction.
  void emitDecodePath(DisassemblerTables &tables) const;

  /// Constructor - Initializes a RecognizableInstr with the appropriate fields
  ///   from a CodeGenInstruction.
  ///
  /// @arg tables - The DisassemblerTables that the specifier will be added to.
  /// @arg insn   - The CodeGenInstruction to extract information from.
  /// @arg uid    - The unique ID of the current instruction.
  RecognizableInstr(DisassemblerTables &tables,
                    const CodeGenInstruction &insn,
                    InstrUID uid);
public:
  /// processInstr - Accepts a CodeGenInstruction and loads decode information
  ///   for it into a DisassemblerTables if appropriate.
  ///
  /// @arg tables - The DiassemblerTables to be populated with decode
  ///               information.
  /// @arg insn   - The CodeGenInstruction to be used as a source for this
  ///               information.
  /// @uid        - The unique ID of the instruction.
  static void processInstr(DisassemblerTables &tables,
                           const CodeGenInstruction &insn,
                           InstrUID uid);
};
  
} // namespace X86Disassembler

} // namespace llvm

#endif
