// ============================================================================
// RegisterPasses.h — OLLVM Obfuscator In-Tree Pass Registration
//
// Declares the entry point for registering OLLVM obfuscation passes directly
// into LLVM's PassBuilder (in-tree, no dynamic plugin loading required).
//
// This header is included by PassBuilder.cpp to call registerOLLVMPasses()
// during PassBuilder construction, making all OLLVM passes available
// permanently in the built clang binary.
//
// Usage in PassBuilder.cpp constructor:
//   registerOLLVMPasses(*this);
//
// Passes are enabled at compile time via -mllvm flags:
//   clang -mllvm -fla -mllvm -bcf -mllvm -sub test.c
// ============================================================================

#ifndef LLVM_OBFUSCATION_REGISTER_PASSES_H
#define LLVM_OBFUSCATION_REGISTER_PASSES_H

namespace llvm {

class PassBuilder;

/// Register all OLLVM obfuscation passes with the given PassBuilder.
/// Call this once during PassBuilder construction.
/// Passes are enabled/disabled via cl::opt flags (-mllvm -fla, etc.)
void registerOLLVMPasses(PassBuilder &PB);

} // namespace llvm

#endif // LLVM_OBFUSCATION_REGISTER_PASSES_H
