###############################################################################
# Unit tests for the no-OS libiio port
###############################################################################

option(TESTS_DEBUG "Print per-assertion test output" ON)

set(NOOS_TEST_INCLUDES
  ${COMMON_INCLUDES}
  ${LIBIIO_DIR}/tests/api
)

add_executable(test_val_noos
  ${CMAKE_CURRENT_SOURCE_DIR}/test_val.c
  ${CMAKE_CURRENT_SOURCE_DIR}/../drivers/iio_val.c
)

add_executable(test_adc_noos
  ${CMAKE_CURRENT_SOURCE_DIR}/test_adc.c
  ${LIBIIO_SRCS}
  ${CMAKE_CURRENT_SOURCE_DIR}/../backend.c
  ${IIO_ADC_SRCS}
)

add_executable(test_backend_noos
  ${CMAKE_CURRENT_SOURCE_DIR}/test_backend.c
  ${LIBIIO_SRCS}
  ${CMAKE_CURRENT_SOURCE_DIR}/../backend.c
)

foreach(target test_val_noos test_adc_noos test_backend_noos)
  target_include_directories(${target} PRIVATE ${NOOS_TEST_INCLUDES})
  target_compile_definitions(${target} PRIVATE ${COMMON_DEFS})
  target_compile_options(${target} PRIVATE -Wall -Wextra
                         -Wno-unused-parameter)

  if(TESTS_DEBUG)
    target_compile_definitions(${target} PRIVATE TESTS_DEBUG)
  endif()

  string(REPLACE "_noos" "" test_name ${target})
  add_test(NAME ${test_name} COMMAND ${target})
endforeach()
