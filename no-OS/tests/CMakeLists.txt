###############################################################################
# Host and hardware API tests for the no-OS libiio port
###############################################################################

option(TESTS_DEBUG "Print per-assertion test output" ON)

set(NOOS_TEST_NAMES
  test_context
  test_device
  test_channel
  test_attr
  test_rw
  test_buffer
)

set(NOOS_TEST_HAL ${CMAKE_CURRENT_SOURCE_DIR}/../drivers/adc/adc_demo/adc_demo.c)

set(NOOS_TEST_INCLUDES
  ${COMMON_INCLUDES}
  ${LIBIIO_DIR}/tests/api
)

set(NOOS_REMOTE_LIB ${LIBIIO_DIR}/build/libiio.so)

foreach(name ${NOOS_TEST_NAMES})
  add_executable(${name}_noos
    ${CMAKE_CURRENT_SOURCE_DIR}/${name}.c
    ${LIBIIO_SRCS}
    ${CMAKE_CURRENT_SOURCE_DIR}/../backend.c
    ${IIO_ADC_SRCS}
    ${NOOS_TEST_HAL}
  )

  target_include_directories(${name}_noos PRIVATE ${NOOS_TEST_INCLUDES})
  target_compile_definitions(${name}_noos PRIVATE ${COMMON_DEFS} TEST_CTX_NOOS)
  target_compile_options(${name}_noos PRIVATE -Wall -Wextra
                         -Wno-unused-parameter)

  if(TESTS_DEBUG)
    target_compile_definitions(${name}_noos PRIVATE TESTS_DEBUG)
  endif()

  add_test(NAME ${name} COMMAND ${name}_noos)
endforeach()

if(EXISTS ${NOOS_REMOTE_LIB})
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
else()
  message(STATUS "no-OS tests: ${NOOS_REMOTE_LIB} not found, "
                 "skipping the remote test flavor")
endif()
