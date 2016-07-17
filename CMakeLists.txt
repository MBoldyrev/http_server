cmake_minimum_required (VERSION 2.8)

set(PROJECT server)

project (${PROJECT})

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -std=gnu99 -Wall -Wfatal-errors -g")

set (HEADERS
    src/fd_pass.h
    src/worker.h
)
set (SOURCES
    src/main.c
    src/fd_pass.c
    src/worker.c
)

#source_group ("Header Files" FILES ${HEADERS})
#source_group ("Source Files" FILES ${SOURCES})

add_executable (final ${HEADERS} ${SOURCES})

target_link_libraries(final ev)
