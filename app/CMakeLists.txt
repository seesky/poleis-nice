set(APP_SOURCES
    appserver.cpp
    appclient.cpp
    sendfile.cpp
    recvfile.cpp
    test.cpp
    appniceserver.cpp
    appniceclient.cpp
)

find_package(Threads)

foreach(src ${APP_SOURCES})
  get_filename_component(exe ${src} NAME_WE)
  add_executable(${exe} ${src})
  target_link_libraries(${exe} PRIVATE udt Threads::Threads m)
  target_compile_options(${exe} PRIVATE -Wall -finline-functions -O3)
  target_include_directories(${exe} PRIVATE ${CMAKE_SOURCE_DIR}/src)
endforeach()
