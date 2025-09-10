set(SOURCES
    api.cpp
    buffer.cpp
    cache.cpp
    ccc.cpp
    channel.cpp
    common.cpp
    core.cpp
    epoll.cpp
    list.cpp
    md5.cpp
    nice_channel.cpp
    packet.cpp
    queue.cpp
    window.cpp
)

find_package(PkgConfig)
if(PkgConfig_FOUND)
  pkg_check_modules(NICE nice)
endif()

add_library(udt SHARED ${SOURCES})
add_library(udt_static STATIC ${SOURCES})
set_target_properties(udt_static PROPERTIES OUTPUT_NAME udt)

if(WIN32)
  foreach(tgt udt udt_static)
    target_link_libraries(${tgt} PRIVATE ws2_32)
  endforeach()
endif()

# Common include directories for public headers
foreach(tgt udt udt_static)
  target_include_directories(${tgt}
    PUBLIC
      $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
      $<INSTALL_INTERFACE:include>
  )
  target_compile_options(${tgt} PRIVATE -fPIC -Wall -Wextra -finline-functions -O3 -fno-strict-aliasing -fvisibility=hidden)
  target_compile_definitions(${tgt} PRIVATE UDT_EXPORTS)
endforeach()

if(NICE_FOUND)
  foreach(tgt udt udt_static)
    target_include_directories(${tgt} PRIVATE ${NICE_INCLUDE_DIRS})
    target_link_libraries(${tgt} PUBLIC ${NICE_LIBRARIES})
    target_compile_options(${tgt} PRIVATE ${NICE_CFLAGS_OTHER})
  endforeach()
endif()
