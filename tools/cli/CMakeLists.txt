add_executable(lyko main.cpp process.cpp)
target_link_libraries(lyko PRIVATE lykoware::engine)
lykoware_configure_target(lyko)
