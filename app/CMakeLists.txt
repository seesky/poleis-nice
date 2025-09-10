set(APP_SOURCES
    appserver.cpp
    appclient.cpp
    sendfile.cpp
    recvfile.cpp
    test.cpp
    appniceserver.cpp
    appniceclient.cpp
)

option(UDT_COPY_DLL "Copy udt.dll beside executables for dynamic linking" OFF)

find_package(Threads)

foreach(src ${APP_SOURCES})
  get_filename_component(exe ${src} NAME_WE)
  add_executable(${exe} ${src})
  if(WIN32)
    target_link_libraries(${exe} PRIVATE udt_static Threads::Threads m ws2_32)
    target_compile_definitions(${exe} PRIVATE UDT_EXPORTS)
    if(UDT_COPY_DLL)
      add_custom_command(TARGET ${exe} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_FILE:udt>" "$<TARGET_FILE_DIR:${exe}>"
      )
    endif()
  else()
    target_link_libraries(${exe} PRIVATE udt Threads::Threads m)
  endif()
  target_compile_options(${exe} PRIVATE -Wall -finline-functions -O3)
  target_include_directories(${exe} PRIVATE ${CMAKE_SOURCE_DIR}/src)
endforeach()
