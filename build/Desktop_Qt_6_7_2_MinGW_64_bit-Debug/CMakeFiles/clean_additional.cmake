# Additional clean files
cmake_minimum_required(VERSION 3.16)

if("${CONFIG}" STREQUAL "" OR "${CONFIG}" STREQUAL "Debug")
  file(REMOVE_RECURSE
  "CMakeFiles\\DBMS_autogen.dir\\AutogenUsed.txt"
  "CMakeFiles\\DBMS_autogen.dir\\ParseCache.txt"
  "DBMS_autogen"
  )
endif()
