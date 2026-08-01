# ============================================================================
# CMakeLists.txt — OLLVM standalone loadable plugin (DLL)
#
# This builds the OLLVM obfuscation passes as a standalone plugin DLL
# that can be loaded via:
#   clang -fpass-plugin=OLLVM-Obfuscator.dll ...
#
# Uses standard CMake add_library(MODULE) instead of LLVM-specific
# add_llvm_loadable_module (which is not available in LLVM 19).
# ============================================================================

# Build as a MODULE library (DLL on Windows, .so on Linux)
add_library(OLLVM-Obfuscator MODULE
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
)

# Enable C++17 (required by LLVM 19)
target_compile_features(OLLVM-Obfuscator PRIVATE cxx_std_17)

# Include paths: Obfuscation/ parent dir + LLVM headers + Obfuscation dir
target_include_directories(OLLVM-Obfuscator PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/..
  ${LLVM_MAIN_INCLUDE_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/../Obfuscation
)

# Link against LLVM libraries (available as CMake targets within the build tree)
target_link_libraries(OLLVM-Obfuscator PRIVATE
  LLVMCore
  LLVMSupport
  LLVMPasses
  LLVMTransformUtils
  LLVMAnalysis
  LLVMIPO
  LLVMScalar
  LLVMInstCombine
  LLVMAggressiveInstCombine
  LLVMVectorize
  LLVMInstrumentation
  LLVMCodeGen
  LLVMTarget
  LLVMIRPrinter
  LLVMObjCARC
  LLVMCoroutines
)

# On Windows, export all symbols from the DLL (needed for llvmGetPassPluginInfo)
# Alternatively, we use __declspec(dllexport) in the source code.
# Keep this as a fallback for any other internal symbols LLVM might need.
if(MSVC)
  set_target_properties(OLLVM-Obfuscator PROPERTIES
    WINDOWS_EXPORT_ALL_SYMBOLS ON
  )
endif()

# Output directory for the plugin DLL
set_target_properties(OLLVM-Obfuscator PROPERTIES
  LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib/OLLVMPlugin"
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/OLLVMPlugin"
)
