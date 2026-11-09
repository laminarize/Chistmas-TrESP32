idf_component_register(
    SRCS
        "app_main.cpp"
        "matter_node.cpp"
        "tree_driver.c"
        "tree_effects.c"
    INCLUDE_DIRS
        "."
        "${ESP_MATTER_PATH}/examples/common/utils"
    REQUIRES
        esp_matter
        esp_driver_ledc
        esp_driver_gpio
        esp_timer
        nvs_flash
        freertos
        esp_wifi
        esp_event
        esp_netif
)

target_compile_options(${COMPONENT_LIB} PRIVATE "-DCHIP_HAVE_CONFIG_H")
