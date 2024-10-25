add_subdirectory(aja-output-ui)
add_subdirectory(decklink-captions)
add_subdirectory(decklink-output-ui)
add_subdirectory(frontend-tools)

option(ENABLE_STREAMFX "Enable StreamFX plugin" ON)
if(ENABLE_STREAMFX)
  add_subdirectory(streamfx)
endif()
