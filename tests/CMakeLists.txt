add_executable(test_foundation_placeholder
  test_foundation_placeholder.cpp
)

target_link_libraries(test_foundation_placeholder
  PRIVATE
    dgcolor_project_options
    dgcolor_parlay
)

add_test(
  NAME foundation_placeholder
  COMMAND test_foundation_placeholder
)

if(NOT DGCOLOR_PARLAY_INCLUDE_DIR STREQUAL "")
  add_executable(test_types_batch
    test_types_batch.cpp
  )

  target_link_libraries(test_types_batch
    PRIVATE
      dgcolor_project_options
      dgcolor_parlay
  )

  add_test(
    NAME types_batch
    COMMAND test_types_batch
  )
else()
  message(WARNING "Skipping test_types_batch because ParlayLib was not found.")
endif()
