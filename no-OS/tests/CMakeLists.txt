###############################################################################
# Hardware API tests for the no-OS libiio port
###############################################################################

option(TESTS_DEBUG "Print per-assertion test output" ON)

set(NOOS_TEST_NAMES
  test_context
  test_device
  test_channel
  test_attr
  test_rw
  test_buffer
  test_concurrent
  test_faults
)

add_executable(test_iio_val
  ${CMAKE_CURRENT_SOURCE_DIR}/test_iio_val.c
  ${CMAKE_CURRENT_SOURCE_DIR}/../drivers/iio_val.c
)

target_include_directories(test_iio_val PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../include
  ${LIBIIO_DIR}/tests/api
)
target_compile_options(test_iio_val PRIVATE -Wall -Wextra
                       -Wno-unused-parameter)

if(TESTS_DEBUG)
  target_compile_definitions(test_iio_val PRIVATE TESTS_DEBUG)
endif()

add_test(NAME test_iio_val COMMAND test_iio_val)

set(NOOS_REMOTE_LIB ${LIBIIO_DIR}/build/libiio.so)

if(NOT EXISTS ${NOOS_REMOTE_LIB})
  message(STATUS "no-OS tests: ${NOOS_REMOTE_LIB} not found, "
                 "build libiio in ${LIBIIO_DIR}/build first")
  return()
endif()

foreach(name ${NOOS_TEST_NAMES})
  add_executable(${name}_remote ${CMAKE_CURRENT_SOURCE_DIR}/${name}.c)

  target_include_directories(${name}_remote PRIVATE
    ${LIBIIO_DIR}/include
    ${LIBIIO_DIR}/tests/api
  )
  target_link_libraries(${name}_remote PRIVATE ${NOOS_REMOTE_LIB})
  set_target_properties(${name}_remote PROPERTIES
                        BUILD_RPATH ${LIBIIO_DIR}/build)
  target_compile_options(${name}_remote PRIVATE -Wall -Wextra
                         -Wno-unused-parameter)

  if(TESTS_DEBUG)
    target_compile_definitions(${name}_remote PRIVATE TESTS_DEBUG)
  endif()
endforeach()
