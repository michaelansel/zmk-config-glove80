if(CONFIG_GLOVE80_BATTERY_TELEMETRY)
  # fw= build id for the G80BAT heartbeat: UTC configure time + git commit.
  # Must never contain whitespace (the host tokenises the line on spaces).
  string(TIMESTAMP G80_FW_TIME "%Y%m%dT%H%M" UTC)
  execute_process(
    COMMAND git -c safe.directory=* -C ${CMAKE_CURRENT_SOURCE_DIR} rev-parse --short HEAD
    OUTPUT_VARIABLE G80_FW_SHA OUTPUT_STRIP_TRAILING_WHITESPACE
    RESULT_VARIABLE G80_GIT_RC ERROR_QUIET)
  if(G80_GIT_RC EQUAL 0 AND G80_FW_SHA)
    execute_process(
      COMMAND git -c safe.directory=* -C ${CMAKE_CURRENT_SOURCE_DIR}
              status --porcelain --untracked-files=no
      OUTPUT_VARIABLE G80_GIT_DIRTY OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
    if(G80_GIT_DIRTY)
      string(APPEND G80_FW_SHA "-dirty")
    endif()
  else()
    set(G80_FW_SHA "nogit")
  endif()
  set(G80_FW_ID "${G80_FW_TIME}-${G80_FW_SHA}")
  message(STATUS "G80BAT fw=${G80_FW_ID}")

  zephyr_include_directories(include)
  target_sources(app PRIVATE src/battery_telemetry.c)
  target_compile_definitions(app PRIVATE G80_FW_ID="${G80_FW_ID}")
endif()
