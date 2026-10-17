cmake_minimum_required(VERSION 3.10)
project(ExVal)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
find_package(Threads REQUIRED)
add_executable(exval_server
    main.cpp common.cpp auth.cpp student.cpp examiner.cpp admin.cpp code_runner.cpp database.cpp
)
target_compile_options(exval_server PRIVATE -Wall -Wextra)
target_include_directories(exval_server PRIVATE /usr/include/mariadb)
target_link_libraries(exval_server PRIVATE mariadb Threads::Threads)
