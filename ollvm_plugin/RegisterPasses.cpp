// ============================================================================
// RegisterPasses.cpp — OLLVM Obfuscator In-Tree Pass Registration
//
// This file is the in-tree replacement for OllvmPlugin.cpp.
// Instead of exporting llvmGetPassPluginInfo() for dynamic plugin loading,
// it provides registerOLLVMPasses() which is called directly from
// PassBuilder.cpp's constructor to permanently register all OLLVM
// obfuscation passes into the built clang binary.
//
// Advantages over the plugin (.dll) approach:
//   - No -fpass-plugin= flag needed
//   - Works with any clang build (including NDK's statically-linked clang)
//   - No DLL loading / symbol export issues on Windows
//   - Passes are always available, enabled via -mllvm flags
//
// This file is placed at: llvm/lib/Passes/Obfuscation/RegisterPasses.cpp
// ============================================================================

#include "RegisterPasses.h"

#include "llvm/Passes/PassBuilder.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/raw_ostream.h"

// Obfuscation pass headers (same directory)
#include "BogusControlFlow.h"
#include "Flattening.h"
#include "SplitBasicBlock.h"
#include "Substitution.h"
#include "StringEncryption.h"
#include "IndirectBranch.h"
#include "IndirectCall.h"
#include "IndirectGlobalVariable.h"
#include "Utils.h"

using namespace llvm;

// ============================================================================
// Command-Line Options
//
// Each obfuscation pass is enabled by a corresponding -mllvm flag.
// Example: clang -mllvm -fla -mllvm -bcf -mllvm -sub test.c
//
// These cl::opt definitions are registered statically when the binary loads,
// so the flags are always available (no plugin loading required).
// ============================================================================

static cl::opt<bool> ObfSplit("split", cl::init(false),
    cl::desc("SplitBasicBlock: split basic blocks before obfuscation"));

static cl::opt<bool> ObfSobf("sobf", cl::init(false),
    cl::desc("String Obfuscation: encrypt string literals"));

static cl::opt<bool> ObfFLA("fla", cl::init(false),
    cl::desc("Flattening: control flow flattening (obfuscate CFG)"));

static cl::opt<bool> ObfSub("sub", cl::init(false),
    cl::desc("Substitution: instruction substitution (obfuscate arithmetic)"));

static cl::opt<bool> ObfBCF("bcf", cl::init(false),
    cl::desc("BogusControlFlow: insert dead code and opaque predicates"));

static cl::opt<bool> ObfIBR("ibr", cl::init(false),
    cl::desc("Indirect Branch: convert direct branches to indirect"));

static cl::opt<bool> ObfICall("icall", cl::init(false),
    cl::desc("Indirect Call: convert direct calls to indirect"));

static cl::opt<bool> ObfIGV("igv", cl::init(false),
    cl::desc("Indirect Global Variable: obfuscate global variable access"));

static cl::opt<bool> ObfFnCmd("fncmd", cl::init(false),
    cl::desc("Use function name to control obfuscation (_ + cmd + _)"));

// Obfuscation function name control flag (defined in Utils.h)
extern bool obf_function_name_cmd;

// ============================================================================
// In-Tree Registration
// ============================================================================

/// Register all OLLVM obfuscation passes with the PassBuilder.
/// Called once from PassBuilder.cpp's constructor.
///
/// Uses registerPipelineStartEPCallback to insert obfuscation passes
/// at the beginning of the optimization pipeline, before standard
/// LLVM optimizations run.
void llvm::registerOLLVMPasses(PassBuilder &PB) {
  PB.registerPipelineStartEPCallback(
    [](ModulePassManager &MPM, OptimizationLevel Level) {
      // Set the function name control flag
      obf_function_name_cmd = ObfFnCmd;

      if (ObfSobf || ObfSplit || ObfFLA || ObfSub || ObfBCF ||
          ObfIBR || ObfICall || ObfIGV) {
        outs() << "[OLLVM] Obfuscation passes enabled.\n";
      }

      // --- Module Pass: String Encryption ---
      // Run first so encrypted strings are in place before block splitting
      MPM.addPass(StringEncryptionPass(ObfSobf));

      // --- Function Passes (wrapped in Module->Function adaptor) ---
      FunctionPassManager FPM;

      // Indirect call obfuscation
      FPM.addPass(IndirectCallPass(ObfICall));

      // Basic block splitting (run before fla/bcf/sub for better results)
      FPM.addPass(SplitBasicBlockPass(ObfSplit));

      // Control flow flattening
      FPM.addPass(FlatteningPass(ObfFLA));

      // Instruction substitution
      FPM.addPass(SubstitutionPass(ObfSub));

      // Bogus control flow
      FPM.addPass(BogusControlFlowPass(ObfBCF));

      // Wrap function passes and add to module pipeline
      MPM.addPass(createModuleToFunctionPassAdaptor(std::move(FPM)));

      // --- Module Passes (run after function passes) ---
      // Indirect branch obfuscation
      MPM.addPass(IndirectBranchPass(ObfIBR));

      // Indirect global variable obfuscation
      MPM.addPass(IndirectGlobalVariablePass(ObfIGV));
    }
  );
}
