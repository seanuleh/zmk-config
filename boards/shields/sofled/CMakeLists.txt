# Sofle custom status screen (portrait, animated Claude critter on left,
# WPM graph on right). Replaces nice_view's built-in status screen.
if(CONFIG_ZMK_DISPLAY AND CONFIG_SOFLED_CUSTOM_STATUS_SCREEN)
  # ZMK's public headers (zmk/event_manager.h, events/, etc.) live in app/include
  zephyr_library_include_directories(${CMAKE_SOURCE_DIR}/include)
  zephyr_library_include_directories(widgets)
  zephyr_library_sources(widgets/sofled_status_screen.c)
  zephyr_library_sources(widgets/dude_sprites.c)

  if(CONFIG_SHIELD_SOFLED_LEFT)
    zephyr_library_sources(widgets/dude_widget.c)
  endif()

  if(CONFIG_SHIELD_SOFLED_RIGHT)
    zephyr_library_sources(widgets/wpm_graph_widget.c)
  endif()
endif()
