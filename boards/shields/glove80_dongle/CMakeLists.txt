if(CONFIG_ZMK_DISPLAY)
  target_sources(app PRIVATE custom_status_screen.c)
endif()

target_sources(app PRIVATE info_dump.c)
