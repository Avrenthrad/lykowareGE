add_executable(lykoware_sandbox main.cpp)
target_link_libraries(lykoware_sandbox PRIVATE lykoware::engine)

lykoware_configure_target(lykoware_sandbox)
