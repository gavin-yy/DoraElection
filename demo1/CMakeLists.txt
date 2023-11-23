cmake_minimum_required(VERSION 3.0.0)

project(demo1 VERSION 1.0.0)

message(">>>PROJECT_NAME:${PROJECT_NAME}")

set(SRCS
    main.c
    global.c

    aciga_util/src/log.c
    aciga_util/src/mainloop.c
    aciga_util/src/mutex.c
    aciga_util/src/timer.c
    aciga_util/src/time.c
    aciga_util/src/mem.c
    aciga_util/src/base64.c
    aciga_util/src/net.c
)

if( USE_BROADCAST )
	add_definitions( -DUSE_BROADCAST)
	set(SRCS ${SRCS} udp_broadcast.c )
else()
	set(SRCS ${SRCS} udp_multicast.c )
endif()

include_directories(${CMAKE_CURRENT_LIST_DIR}/../src)
include_directories(aciga_util/include)
include_directories(aciga_util/thirdparty/uthash/src)


add_definitions("-DUSE_EPOLL")

add_executable(${PROJECT_NAME} ${SRCS} )

target_link_libraries(${PROJECT_NAME} dora_election )


install(TARGETS ${PROJECT_NAME} RUNTIME DESTINATION bin)
