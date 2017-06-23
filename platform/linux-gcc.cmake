# this one is important
set(CMAKE_SYSTEM_NAME Linux)
 
#this one not so much
set(CMAKE_SYSTEM_VERSION 1)

# specify the cross compiler
set(CMAKE_C_COMPILER   /usr/bin/gcc)
set(CMAKE_CXX_COMPILER /usr/bin/g++)

# Compiler flags
set(CXXFLAGS "-Wall -pedantic -Wextra")
include_directories(/usr/include)
link_directories(/usr/lib)
link_libraries(curl)
