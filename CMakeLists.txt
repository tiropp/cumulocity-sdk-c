cmake_minimum_required(VERSION 3.6)
project(cumulocity-sdk)


set(SR_PLUGIN_LUA 0)
set(SR_PROTO_HTTP_VERSION 1.1)
set(SR_SOCK_RXBUF_SIZE 1024)
set(SR_AGENT_VAL 5)
set(SR_REPORTER_NUM 512)
set(SR_REPORTER_VAL 400)
set(SR_REPORTER_RETRIES 9)
set(SR_CURL_SIGNAL 1)
set(SR_SSL_VERIFYCERT 1)
set(SR_FILEBUF_PAGE_SCALE 3)

set(BUILD debug)

set(SRC_DIR src)
set(MQTT_SRC_DIR ext/pahomqtt/MQTTPacket/src)

set(LIB_DIR lib)
set(LIBNAME sera)
set(LIBSOVERSION 1)
set(LIBVERSION "1.2.0")


##
## srwatchdogd
##
add_executable(srwatchdogd
  ${SRC_DIR}/watchdog/srwatchdogd.cc
  )
set_target_properties(srwatchdogd PROPERTIES
  COMPILE_FLAGS "-std=c++11 -Os -s -DNDEBUG -flto -fno-exceptions -fno-rtti -fno-stack-protector")


##
## sera
##
file(GLOB SRC RELATIVE "${CMAKE_SOURCE_DIR}" "${SRC_DIR}/*.cc")
file(GLOB MQTT_SRC RELATIVE "${CMAKE_SOURCE_DIR}" "${MQTT_SRC_DIR}/*.c")
if( ${SR_PLUGIN_LUA} EQUAL 0 )
  list(REMOVE_ITEM SRC "src/srluapluginmanager.cc")
endif()

set(CFLAGS "${CFLAGS} -fPIC -pipe -MMD")
set(CXXFLAGS "${CXXFLAGS} -std=c++11 -fPIC -pipe -pthread -MMD")
if( "${SR_PROTO_HTTP_VERSION}" STREQUAL "1.0" )
  set(CPPFLAGS "${CPPFLAGS} -DSR_HTTP_1_0")
endif()
if( "${BUILD}" STREQUAL "release" )
  set(CPPFLAGS "${CPPFLAGS} -DNDEBUG")
  set(CXXFLAGS "${CXXFLAGS} -O2 -ffast-math -flto")
  set(CFLAGS "${CFLAGS} -O2 -ffast-math -flto")
  set(LDFLAGS "${LDFLAGS} -O2 -s")
else()
  set(CPPFLAGS "${CPPFLAGS} -DDEBUG")
  set(CXXFLAGS "${CXXFLAGS} -O0 -g")
  set(CFLAGS "${CFLAGS} -O0 -g")
  set(LDFLAGS "${LDFLAGS} -O0 -g")
endif()
set(LDFLAGS "${LDFLAGS} -Wl,--no-undefined")
set(LDLIBS "$ENV{LDLIBS}")
list(APPEND LDLIBS "pthread")

add_library(${LIBNAME} SHARED ${SRC} ${MQTT_SRC})
target_include_directories(${LIBNAME} PRIVATE include)
target_compile_definitions(${LIBNAME} PRIVATE
  -DSR_SOCK_RXBUF_SIZE=${SR_SOCK_RXBUF_SIZE}
  -DSR_AGENT_VAL=${SR_AGENT_VAL}
  -DSR_REPORTER_NUM=${SR_REPORTER_NUM}
  -DSR_REPORTER_VAL=${SR_REPORTER_VAL}
  -DSR_REPORTER_RETRIES=${SR_REPORTER_RETRIES}
  -DSR_CURL_SIGNAL=${SR_CURL_SIGNAL}
  -DSR_SSL_VERIFYCERT=${SR_SSL_VERIFYCERT}
  -DSR_FILEBUF_PAGE_SCALE=${SR_FILEBUF_PAGE_SCALE}
  )
set_source_files_properties(${MQTT_SRC} PROPERTIES LANGUAGE C   COMPILE_FLAGS "${CPPFLAGS} ${CFLAGS}")
set_source_files_properties(${SRC}      PROPERTIES LANGUAGE CXX COMPILE_FLAGS "${CPPFLAGS} ${CXXFLAGS}")

set_target_properties(${LIBNAME} PROPERTIES
  LINK_FLAGS "${LDFLAGS}"
  SOVERSION ${LIBSOVERSION}
  VERSION ${LIBVERSION}
  )
target_link_libraries(${LIBNAME} ${LDLIBS})



##
## tests
##
enable_testing()
file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/tests)
file(GLOB TEST_SRC "tests/test_*.cc")
foreach(src ${TEST_SRC})
  string(REGEX REPLACE "\(.*\)\/\(test_.*\).cc" "\\2" bin ${src})
  add_executable(${bin} ${src})
  target_include_directories(${bin} PRIVATE include)
  target_compile_options(${bin} PRIVATE -std=c++11)
  target_link_libraries(${bin} pthread sera)

  add_test(NAME ${bin} COMMAND ${bin})
endforeach()

