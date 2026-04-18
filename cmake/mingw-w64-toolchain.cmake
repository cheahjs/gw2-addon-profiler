set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)

set(CMAKE_C_COMPILER x86_64-w64-mingw32-gcc)
set(CMAKE_CXX_COMPILER x86_64-w64-mingw32-g++)
set(CMAKE_RC_COMPILER x86_64-w64-mingw32-windres)

set(CMAKE_FIND_ROOT_PATH /usr/x86_64-w64-mingw32)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)

# Static link GCC/C++ runtimes so the DLL is self-contained.
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -static-libgcc -static-libstdc++")
set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -static-libgcc -static-libstdc++")

# Resolve the full path to the static winpthread archive. The g++
# driver implicitly links libwinpthread-1.dll at the end of every
# link, which cannot be overridden with -Wl,-Bstatic ordering.
# Linking the .a by full path ensures static resolution.
execute_process(
    COMMAND ${CMAKE_CXX_COMPILER} -print-file-name=libwinpthread.a
    OUTPUT_VARIABLE MINGW_WINPTHREAD_STATIC
    OUTPUT_STRIP_TRAILING_WHITESPACE)
set(MINGW_WINPTHREAD_STATIC "${MINGW_WINPTHREAD_STATIC}" CACHE FILEPATH "" FORCE)
