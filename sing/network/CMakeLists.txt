# MIT License
# Copyright (c) 2026 dbjwhs

# Sing Network - TCP and discovery integration tests

add_subdirectory(tcp_calculator)
add_subdirectory(discovery)
add_subdirectory(secure)

# Convenience target to build all network tests
add_custom_target(sing_network_all
    DEPENDS
        sing_network_tcp_calculator
        sing_network_discovery
        sing_network_secure
)
