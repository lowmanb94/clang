set(LLVM_LINK_COMPONENTS
  Support
  )

add_clang_library(clangStaticAnalyzerCheckers
  ClangCheckers.cpp
  CheckerDocumentation.cpp
  LockSetChecker.cpp
  RetainCountChecker.cpp
  AllocationDiagnostics.cpp

  DEPENDS
  ClangSACheckers

  LINK_LIBS
  clangAST
  clangASTMatchers
  clangAnalysis
  clangBasic
  clangLex
  clangStaticAnalyzerCore
  )
