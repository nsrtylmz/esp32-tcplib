idf_component_register(
    SRCS "src/tcp_internal.cpp" "src/TcpClient.cpp" "src/TcpListener.cpp"
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "src"
    REQUIRES esp_netif
    PRIV_REQUIRES lwip esp_timer freertos)
