# ============================================================================
# CMakeLists.txt — OLLVM standalone loadable plugin (DLL)
#
# This builds the OLLVM obfuscation passes as a standalone plugin DLL
# that can be loaded via:
#   clang -fpass-plugin=OLLVM-Obfuscator.dll ...
#
# The plugin is built as a loadable module using add_llvm_loadable_module,
# which is the standard LLVM mechanism for out-of-tree pass plugins.
# ============================================================================

# Define the loadable module.
# add_llvm_loadable_module creates a shared library (DLL on Windows)
# that LLVM's plugin loader can discover and load at runtime.
add_llvm_loadable_module(OLLVM-Obfuscator
  # Plugin entry point — registers passes via llvm::PassPluginLibraryInfo
  OllvmPlugin.cpp

  # Obfuscation pass source files
  ${CMAKE_CURRENT_SOURCE_DIR}/../Obfuscation/BogusControlFlow.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/../Obfuscation/CryptoUtils.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/../Obfuscation/Flattening.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/../Obfuscation/IPObfuscationContext.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/../Obfuscation/ObfuscationOptions.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/../Obfuscation/SplitBasicBlock.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/../Obfuscation/StringEncryption.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/../Obfuscation/Substitution.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/../Obfuscation/Utils.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/../Obfuscation/IndirectBranch.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/../Obfuscation/IndirectCall.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/../Obfuscation/IndirectGlobalVariable.cpp

  ADDITIONAL_HEADER_DIRS
  ${CMAKE_CURRENT_SOURCE_DIR}/../Obfuscation

  DEPENDS
  intrinsics_gen

  LINK_COMPONENTS
  Core
  Support
  Passes
  TransformUtils
  Analysis
  IPO
  Scalar
  InstCombine
  AggressiveInstCombine
  Vectorize
  Instrumentation
  CodeGen
  Target
  IRPrinter
  ObjCARC
  Coroutines
)

# Enable C++17 (required by LLVM 19)
target_compile_features(OLLVM-Obfuscator PRIVATE cxx_std_17)

# The Obfuscation source files use #include "Obfuscation/xxx.h" patterns,
# so we need to add the parent directory of Obfuscation/ to the include path.
target_include_directories(OLLVM-Obfuscator PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/..
  ${LLVM_MAIN_INCLUDE_DIR}
)

# The Obfuscation source files also include "CryptoUtils.h" directly,
# so we add the Obfuscation directory itself to the include path.
target_include_directories(OLLVM-Obfuscator PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../Obfuscation
)

# Set output directory for the plugin DLL
set_target_properties(OLLVM-Obfuscator PROPERTIES
  LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib/OLLVMPlugin"
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/OLLVMPlugin"
)
