cmake_minimum_required(VERSION 4.0)
project(OddProg C CXX)

cmake_policy(SET CMP0135 NEW)

find_program(SDCC_EXECUTABLE sdcc REQUIRED)
if(NOT SDCC_EXECUTABLE)
  message(FATAL_ERROR "sdcc not found. Install sdcc and make sure it is in PATH.")
endif()

set(MAIN_SOURCE ${CMAKE_SOURCE_DIR}/main.c)
set(DEVICE_SOURCE ${CMAKE_SOURCE_DIR}/device.c)
set(PROG_SOURCE ${CMAKE_SOURCE_DIR}/prog.c)
set(PROTOCOL_SOURCE ${CMAKE_SOURCE_DIR}/protocol.c)
set(OUT_IHX ${CMAKE_BINARY_DIR}/OddProg.ihx)
set(OUT_BIN ${CMAKE_BINARY_DIR}/OddProg.bin)

# 8051 memory settings
set(ROM_SIZE 0x2000)          # 8 KB code space, adjust as needed
set(RAM_SIZE 0x100)           # 256 bytes internal ram
set(XRAM_SIZE 0x100 CACHE STRING "External RAM size for SDCC --xram-size")
set(SDCC_MODEL small CACHE STRING "SDCC memory model: small|medium|large")
set_property(CACHE SDCC_MODEL PROPERTY STRINGS small medium large)
set(EXTRA_SDCC_FLAGS --code-size ${ROM_SIZE} --xram-loc 0x0100 --xram-size ${XRAM_SIZE} --model-${SDCC_MODEL})
set(SDCC_COMPILE_FLAGS -mmcs51 -c -DSTC89 ${EXTRA_SDCC_FLAGS})
set(SDCC_LINK_FLAGS -mmcs51 --out-fmt-ihx -DSTC89 ${EXTRA_SDCC_FLAGS})

set(FIRMWARE_REL_FILES
  ${CMAKE_BINARY_DIR}/main.rel
  ${CMAKE_BINARY_DIR}/device.rel
  ${CMAKE_BINARY_DIR}/prog.rel
  ${CMAKE_BINARY_DIR}/protocol.rel
)

add_custom_command(
  OUTPUT ${FIRMWARE_REL_FILES}
  COMMAND ${SDCC_EXECUTABLE} ${SDCC_COMPILE_FLAGS} -o ${CMAKE_BINARY_DIR}/main.rel ${MAIN_SOURCE}
  COMMAND ${SDCC_EXECUTABLE} ${SDCC_COMPILE_FLAGS} -o ${CMAKE_BINARY_DIR}/device.rel ${DEVICE_SOURCE}
  COMMAND ${SDCC_EXECUTABLE} ${SDCC_COMPILE_FLAGS} -o ${CMAKE_BINARY_DIR}/prog.rel ${PROG_SOURCE}
  COMMAND ${SDCC_EXECUTABLE} ${SDCC_COMPILE_FLAGS} -o ${CMAKE_BINARY_DIR}/protocol.rel ${PROTOCOL_SOURCE}
  DEPENDS ${MAIN_SOURCE} ${DEVICE_SOURCE} ${PROG_SOURCE} ${PROTOCOL_SOURCE}
  COMMENT "Compiling 8051 firmware sources to SDCC object files"
)

add_custom_command(
  OUTPUT ${OUT_IHX}
  COMMAND ${SDCC_EXECUTABLE} ${SDCC_LINK_FLAGS} -o ${OUT_IHX} ${FIRMWARE_REL_FILES}
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  DEPENDS ${FIRMWARE_REL_FILES}
  COMMENT "Linking 8051 firmware objects into ${OUT_IHX}"
)

add_custom_target(OddProgTester ALL
  DEPENDS ${OUT_IHX}
  COMMENT "Generate 8051 hex firmware"
)

# Optional step: convert .ihx to true binary (.bin) for your toolchain.
find_program(SDOBJCOPY_EXECUTABLE sdobjcopy REQUIRED)
if(NOT SDOBJCOPY_EXECUTABLE)
  message(FATAL_ERROR "sobjcopy not found. Install sdcc tools and make sure it is in PATH.")
endif()

add_custom_command(TARGET OddProgTester POST_BUILD
  COMMAND ${SDOBJCOPY_EXECUTABLE} -I ihex -O binary  ${OUT_IHX} ${OUT_BIN}
  COMMENT "Convert ${OUT_IHX} to binary ${OUT_BIN}"
)

# Enable testing and include tests directory
enable_testing()
add_subdirectory(tests)
