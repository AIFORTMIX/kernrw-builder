// ============================================================================
// OllvmPlugin.cpp — OLLVM Obfuscator LLVM Pass Plugin Entry Point
//
// This is the entry point for the OLLVM standalone pass plugin DLL.
// It registers the obfuscation passes using the LLVM NewPM plugin API
// (llvm::PassPluginLibraryInfo), which allows the plugin to be loaded
// dynamically via:
//   clang -fpass-plugin=OLLVM-Obfuscator.dll [other flags] source.c
//
// The obfuscation passes are enabled individually via command-line flags
// like -fla, -bcf, -sub, etc. Each flag enables a specific obfuscation
// pass. Multiple flags can be combined for layered obfuscation.
// ============================================================================

#include "llvm/Passes/PassPlugin.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/raw_ostream.h"

// Obfuscation pass headers
#include "Obfuscation/BogusControlFlow.h"
#include "Obfuscation/Flattening.h"
#include "Obfuscation/SplitBasicBlock.h"
#include "Obfuscation/Substitution.h"
#include "Obfuscation/StringEncryption.h"
#include "Obfuscation/IndirectBranch.h"
#include "Obfuscation/IndirectCall.h"
#include "Obfuscation/IndirectGlobalVariable.h"
#include "Obfuscation/Utils.h"

using namespace llvm;

// ============================================================================
// Command-Line Options
// Each obfuscation pass is enabled by a corresponding -Xclang -mllvm flag.
// Example: clang -fpass-plugin=OLLVM-Obfuscator.dll -Xclang -mllvm -fla test.c
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
// Plugin Registration
// ============================================================================

/// Entry point callback that registers the OLLVM passes with the pass
/// builder. This callback is invoked by LLVM's plugin loader when the
/// plugin is loaded via -fpass-plugin=.
static void registerOLLVMPassCallbacks(PassBuilder &PB) {
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

// ============================================================================
// Plugin Information Structure
// This is the mandatory entry point that LLVM's plugin loader looks for.
// ============================================================================
PassPluginLibraryInfo getPassPluginInfo() {
  return {
    LLVM_PLUGIN_API_VERSION,        // API version (must match LLVM version)
    "OLLVM-Obfuscator",             // Plugin name
    "19.1.0",                       // Plugin version
    [](PassBuilder &PB) {
      registerOLLVMPassCallbacks(PB);
    }
  };
}

/// The LLVM plugin loader calls this function to obtain plugin information.
/// The function must be extern "C" and have the exact signature:
///   extern "C" LLVM_ATTRIBUTE_WEAK PassPluginLibraryInfo llvmGetPassPluginInfo()
///
/// NOTE: Do NOT add __declspec(dllexport) here. LLVM 19's PassPlugin.h already
/// declares this function with LLVM_ATTRIBUTE_WEAK (which maps to __declspec(selectany)).
/// Adding dllexport causes "redefinition; different linkage" error on MSVC.
/// The WINDOWS_EXPORT_ALL_SYMBOLS CMake property exports all symbols from the DLL.
extern "C" LLVM_ATTRIBUTE_WEAK ::llvm::PassPluginLibraryInfo
llvmGetPassPluginInfo() {
  return getPassPluginInfo();
}
