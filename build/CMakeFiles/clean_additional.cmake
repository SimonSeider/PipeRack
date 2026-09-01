# Additional clean files
cmake_minimum_required(VERSION 3.16)

if("${CONFIG}" STREQUAL "" OR "${CONFIG}" STREQUAL "Debug")
  file(REMOVE_RECURSE
  "CMakeFiles/piperack_autogen.dir/AutogenUsed.txt"
  "CMakeFiles/piperack_autogen.dir/ParseCache.txt"
  "piperack_autogen"
  )
endif()
