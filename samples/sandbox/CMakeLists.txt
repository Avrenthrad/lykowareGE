add_executable(lykoware_sandbox main.cpp)
target_link_libraries(lykoware_sandbox PRIVATE lykoware::engine)

if(MSVC)
    target_compile_options(lykoware_sandbox PRIVATE /W4 /permissive- /utf-8)
else()
    target_compile_options(lykoware_sandbox PRIVATE -Wall -Wextra -Wpedantic)
endif()
