# MIT License
# Copyright (c) 2026 dbjwhs

# Sing - Song Integration Tests

# IPC tests (local pipe-based communication)
add_subdirectory(ipc)

# Network tests (TCP, mDNS discovery, security)
add_subdirectory(network)

# Convenience target to build all sing projects
add_custom_target(sing_all
    DEPENDS
        sing_ipc_all
        sing_network_all
)
