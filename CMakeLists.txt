cmake_minimum_required(VERSION 3.5)

if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
  if(DEFINED ENV{VITASDK})
    set(CMAKE_TOOLCHAIN_FILE "$ENV{VITASDK}/share/vita.toolchain.cmake" CACHE PATH "toolchain file")
  else()
    message(FATAL_ERROR "Please define VITASDK to point to your SDK path!")
  endif()
endif()

project(Strummer C)
include("${VITASDK}/share/vita.cmake" REQUIRED)

set(VITA_APP_NAME "Vita Strummer")
set(VITA_TITLEID  "STRM00001")
set(VITA_VERSION  "00.98")

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wl,-q -Wall -O2 -std=gnu99")

add_executable(${PROJECT_NAME}
  src/main.c
  src/audio.c
  src/synth.c
  src/theory.c
  src/strum.c
  src/canvas.c
  src/ui.c
  src/midi.c
)

# MIDI по USB: клиент плагина psvita-usb-audio-midi (third_party, из VitaMIDI) собирается в CI отдельно
# в build-usb (см. workflow). Есть он - MIDI включён и клиент кладётся в VPK; нет - заглушки в midi.c.
set(USBMIDI_DIR ${CMAKE_SOURCE_DIR}/third_party/psvita-usb-audio-midi)
set(USBMIDI_BUILD ${CMAKE_SOURCE_DIR}/build-usb CACHE PATH "сборка psvita-usb-audio-midi")
set(VS_MIDI_FILES "")
if(EXISTS ${USBMIDI_BUILD}/psvita_usb_audio_midi_client.suprx)
  message(STATUS "MIDI по USB: включён")
  target_sources(${PROJECT_NAME} PRIVATE ${USBMIDI_DIR}/src/client/loader.c)
  target_include_directories(${PROJECT_NAME} PRIVATE ${USBMIDI_DIR}/include)
  target_compile_definitions(${PROJECT_NAME} PRIVATE VS_MIDI=1)
  target_link_directories(${PROJECT_NAME} PRIVATE ${USBMIDI_BUILD})
  target_link_libraries(${PROJECT_NAME} psvita_usb_audio_midi_client_stub_weak SceKernelModulemgr_stub)
  set(VS_MIDI_FILES FILE ${USBMIDI_BUILD}/psvita_usb_audio_midi_client.suprx psvita_usb_audio_midi_client.suprx)
else()
  message(STATUS "MIDI по USB: build-usb не найден - без MIDI")
endif()

target_link_libraries(${PROJECT_NAME}
  vita2d
  SceDisplay_stub
  SceGxm_stub
  SceSysmodule_stub
  SceCtrl_stub
  SceAppMgr_stub
  SceLibKernel_stub
  SceTouch_stub
  SceAudio_stub
  SceCommonDialog_stub
  ScePgf_stub
  ScePvf_stub
  freetype
  png
  jpeg
  z
  m
  c
)

vita_create_self(${PROJECT_NAME}.self ${PROJECT_NAME})
vita_create_vpk(${PROJECT_NAME}.vpk ${VITA_TITLEID} ${PROJECT_NAME}.self
  VERSION ${VITA_VERSION}
  NAME ${VITA_APP_NAME}
  FILE ${CMAKE_SOURCE_DIR}/sce_sys/icon0.png sce_sys/icon0.png   # иконка (host/make_icon.py)
  ${VS_MIDI_FILES}
)
