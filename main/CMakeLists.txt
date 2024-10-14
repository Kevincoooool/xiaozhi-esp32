set(SOURCES "AudioDevice.cc"
            "FirmwareUpgrade.cc"
            "SystemInfo.cc"
            "SystemReset.cc"
            "Application.cc"
            "Display.cc"
            "Button.cc"
            "main.cc"
            "lv_gui.c"
            "font_dingdingjbt.c"
            "img_src_mic_logo.c"
            )

if(CONFIG_USE_AFE_SR)
    list(APPEND SOURCES "AudioProcessor.cc" "WakeWordDetect.cc")
endif()

idf_component_register(SRCS ${SOURCES}
                    INCLUDE_DIRS "."
                    )
