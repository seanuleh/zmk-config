# Sofle custom status screen (portrait, animated Claude critter on left, WPM graph on right).
# Replaces the nice_view shield's built-in status screen.
if(CONFIG_ZMK_DISPLAY AND CONFIG_SOFLED_CUSTOM_STATUS_SCREEN)
  zephyr_library_include_directories(widgets)
  zephyr_library_sources(widgets/sofled_status_screen.c)
endif()
