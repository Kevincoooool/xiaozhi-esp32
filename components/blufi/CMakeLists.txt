

idf_component_register(
    SRCS 
        "blufi_manager.cpp"
        "blufi_security.cpp"
        "blufi_config.cpp"
    INCLUDE_DIRS 
        "include"
    REQUIRES 
        bt
        mbedtls esp_wifi esp_event
)
