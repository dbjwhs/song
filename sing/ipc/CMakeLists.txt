# MIT License
# Copyright (c) 2026 dbjwhs

# Sing IPC - Local pipe-based integration tests

add_subdirectory(calculator)
add_subdirectory(stockticker)
add_subdirectory(chat)
add_subdirectory(datacopy)

# Convenience target to build all IPC tests
add_custom_target(sing_ipc_all
    DEPENDS
        sing_ipc_calculator
        sing_ipc_stockticker
        sing_ipc_chat
        sing_ipc_datacopy
)
