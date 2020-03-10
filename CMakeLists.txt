cmake_minimum_required (VERSION 3.10)

project (symphony-plugin)

add_library (OptimizeCut MODULE OptimizeCut.cpp)
target_link_libraries (OptimizeCut Sym)
set_target_properties(OptimizeCut PROPERTIES
  CXX_STANDARD 11
  CXX_STANDARD_REQUIRED YES
  CXX_EXTENSIONS NO
  PREFIX ""
)
