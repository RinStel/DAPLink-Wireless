# Cross-compilation toolchain for the GD32F303 firmware.
set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR cortex-m4)
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

if(NOT ARM_GCC_BIN AND DEFINED ENV{ARM_GCC_BIN})
    set(ARM_GCC_BIN "$ENV{ARM_GCC_BIN}" CACHE PATH
        "Directory containing arm-none-eabi-* tools")
endif()
set(_arm_gcc_names arm-none-eabi-gcc)
set(_arm_objcopy_names arm-none-eabi-objcopy)
set(_arm_size_names arm-none-eabi-size)
if(WIN32)
    list(APPEND _arm_gcc_names arm-none-eabi-gcc.exe)
    list(APPEND _arm_objcopy_names arm-none-eabi-objcopy.exe)
    list(APPEND _arm_size_names arm-none-eabi-size.exe)
endif()
find_program(ARM_GCC_COMPILER NAMES ${_arm_gcc_names}
    HINTS "${ARM_GCC_BIN}" REQUIRED)
find_program(ARM_GCC_OBJCOPY NAMES ${_arm_objcopy_names}
    HINTS "${ARM_GCC_BIN}" REQUIRED)
find_program(ARM_GCC_SIZE NAMES ${_arm_size_names}
    HINTS "${ARM_GCC_BIN}" REQUIRED)
get_filename_component(_arm_gcc_dir "${ARM_GCC_COMPILER}" DIRECTORY)
set(CMAKE_C_COMPILER "${ARM_GCC_COMPILER}")
set(CMAKE_ASM_COMPILER "${ARM_GCC_COMPILER}")
set(CMAKE_OBJCOPY "${ARM_GCC_OBJCOPY}" CACHE FILEPATH "Arm objcopy")
set(CMAKE_SIZE "${ARM_GCC_SIZE}" CACHE FILEPATH "Arm size")
set(CMAKE_FIND_ROOT_PATH "${_arm_gcc_dir}")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
