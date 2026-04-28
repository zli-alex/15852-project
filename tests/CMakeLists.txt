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

  add_executable(test_interfaces_compile
    test_interfaces_compile.cpp
  )

  target_link_libraries(test_interfaces_compile
    PRIVATE
      dgcolor_project_options
      dgcolor_parlay
  )

  add_test(
    NAME interfaces_compile
    COMMAND test_interfaces_compile
  )

  add_executable(test_graph_store
    test_graph_store.cpp
    ../src/graph_store.cpp
  )

  target_link_libraries(test_graph_store
    PRIVATE
      dgcolor_project_options
      dgcolor_parlay
  )

  add_test(
    NAME graph_store
    COMMAND test_graph_store
  )
else()
  message(WARNING "Skipping Parlay-dependent tests (test_types_batch, test_interfaces_compile, test_graph_store) because ParlayLib was not found.")
endif()
