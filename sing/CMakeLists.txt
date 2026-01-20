# MIT License
# Copyright (c) 2026 dbjwhs

# Sing - Song Integration Tests

add_subdirectory(calculator)
add_subdirectory(stockticker)
add_subdirectory(chat)
add_subdirectory(datacopy)

# Convenience target to build all sing projects
add_custom_target(sing_all
    DEPENDS
        sing_calculator
        sing_stockticker
        sing_chat
        sing_datacopy
)
