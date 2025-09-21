set(APP_SOURCES
    appserver.cpp
    appclient.cpp
    sendfile.cpp
    recvfile.cpp
    test.cpp
    appniceserver.cpp
    appniceclient.cpp
    appnicefileserver.cpp
    appnicefileclient.cpp
    appgstclient.cpp
    appgstserver.cpp
    nice_channel_retry_test.cpp
    nice_channel_recv_test.cpp
)

option(UDT_COPY_DLL "Copy udt.dll beside executables for dynamic linking" OFF)

find_package(Threads)
find_package(PkgConfig REQUIRED)
pkg_check_modules(GST REQUIRED IMPORTED_TARGET gstreamer-1.0 gstreamer-app-1.0 gstreamer-video-1.0)

set(GST_APP_TARGETS
  appgstclient
  appgstserver
)

foreach(src ${APP_SOURCES})
  get_filename_component(exe ${src} NAME_WE)
  add_executable(${exe} ${src})
  list(FIND GST_APP_TARGETS ${exe} _is_gst_target)
  if(WIN32)
    target_link_libraries(${exe} PRIVATE udt_static Threads::Threads m ws2_32)
    if(_is_gst_target GREATER -1)
      target_link_libraries(${exe} PRIVATE PkgConfig::GST)
    endif()
    target_compile_definitions(${exe} PRIVATE UDT_EXPORTS)
    if(UDT_COPY_DLL)
      add_custom_command(TARGET ${exe} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_FILE:udt>" "$<TARGET_FILE_DIR:${exe}>"
      )
    endif()
  else()
    target_link_libraries(${exe} PRIVATE udt Threads::Threads m)
    if(_is_gst_target GREATER -1)
      target_link_libraries(${exe} PRIVATE PkgConfig::GST)
    endif()
  endif()
  if(_is_gst_target GREATER -1)
    target_compile_options(${exe} PRIVATE ${GST_CFLAGS_OTHER})
    target_include_directories(${exe} PRIVATE ${GST_INCLUDE_DIRS})
  endif()
  target_compile_options(${exe} PRIVATE -Wall -finline-functions -O3)
  target_include_directories(${exe} PRIVATE ${CMAKE_SOURCE_DIR}/src)
endforeach()
