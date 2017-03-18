cmake_minimum_required(VERSION 2.8)

if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
  if(DEFINED ENV{VITASDK})
    set(CMAKE_TOOLCHAIN_FILE "$ENV{VITASDK}/share/vita.toolchain.cmake" CACHE PATH "toolchain file")
  else()
    message(FATAL_ERROR "Please define VITASDK to point to your SDK path!")
  endif()
endif()

project(taishell)
include("${VITASDK}/share/vita.cmake" REQUIRED)

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wl,-q -Wall -O3 -std=gnu99")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11 -fno-rtti -fno-exceptions")

link_directories(
  ${CMAKE_CURRENT_BINARY_DIR}
  ${CMAKE_CURRENT_BINARY_DIR}/streamer-stubs
)

add_executable(streamer_kernel
  kernel.c
)

target_link_libraries(streamer_kernel
  taihenForKernel_stub
  TaishellForKernel_stub
  SceSysmemForDriver_stub
  SceSysclibForDriver_stub
  SceNetPsForDriver_stub
  SceModulemgrForKernel_stub
  SceModulemgrForDriver_stub
  SceThreadmgrForDriver_stub
)

set_target_properties(streamer_kernel
  PROPERTIES LINK_FLAGS "-nostdlib"
  COMPILE_FLAGS "-D__VITA_KERNEL__"
)

vita_create_self(kstreamer.skprx streamer_kernel
  UNSAFE
  CONFIG ${CMAKE_CURRENT_SOURCE_DIR}/kernel.yml
)

vita_create_stubs(streamer-stubs streamer_kernel ${CMAKE_CURRENT_SOURCE_DIR}/kernel.yml
  KERNEL
)

install(DIRECTORY ${CMAKE_BINARY_DIR}/streamer-stubs/
  DESTINATION lib
  FILES_MATCHING PATTERN "*.a"
)

# USER

add_executable(streamer_user
  user.c
)

add_dependencies(streamer_user streamer-stubs)

target_link_libraries(streamer_user
  taihen_stub
  Taishell_stub
  TaishellGUI_stub
  Streamer_stub
  SceSysmem_stub
  SceLibKernel_stub
  SceAppmgr_stub
  SceKernelThreadmgr_stub
  SceKernelModulemgr_stub
  SceProcessmgr_stub
  SceNet_stub
  SceNetCtl_stub
  SceSysmodule_stub
  SceJpegEnc_stub
  SceIofilemgr_stub
)

set_target_properties(streamer_user
  PROPERTIES LINK_FLAGS "-nostdlib"
)

vita_create_self(streamer.suprx streamer_user
  UNSAFE
  CONFIG ${CMAKE_CURRENT_SOURCE_DIR}/user.yml
)