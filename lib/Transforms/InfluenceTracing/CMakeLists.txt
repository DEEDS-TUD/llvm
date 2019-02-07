add_llvm_library(LLVMInfluenceTracing
  InfluenceTracing.cpp
  ex3.cpp

  ADDITIONAL_HEADER_DIRS
  ${LLVM_MAIN_INCLUDE_DIR}/llvm/Transforms
  ${LLVM_MAIN_INCLUDE_DIR}/llvm/Transforms/InfluenceTracing

  DEPENDS
  intrinsics_gen
  )
