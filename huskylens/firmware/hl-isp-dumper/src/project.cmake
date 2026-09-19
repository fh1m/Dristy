set_target_properties(isp_stub PROPERTIES
    LINK_FLAGS "-Wl,-Map=${CMAKE_BINARY_DIR}/isp_stub.map")
if(ISP_HEADLESS)
    target_compile_definitions(isp_stub PRIVATE ISP_HEADLESS=1)
endif()
if(ISP_LCD_INIT_ONLY)
    target_compile_definitions(isp_stub PRIVATE ISP_LCD_INIT_ONLY=1)
endif()
