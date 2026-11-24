# The library is the repository root. ESP-IDF names a component after its
# folder, so this is "tcplib", or "esp32-tcplib" in a fresh clone from GitHub.
get_filename_component(tcplib_dir "${CMAKE_CURRENT_LIST_DIR}/../../.." ABSOLUTE)
get_filename_component(tcplib_component "${tcplib_dir}" NAME)

idf_component_register(
    SRCS main.cpp
    PRIV_REQUIRES
        ${tcplib_component}         # this library (see above)
        protocol_examples_common    # example_connect()
        nvs_flash
        esp_netif
        esp_event
        esp_timer
        esp_wifi                    # esp_wifi_set_ps()
)
