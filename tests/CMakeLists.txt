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
  COMMAND $<TARGET_FILE:test_foundation_placeholder>
)

add_executable(test_rng
  test_rng.cpp
  ../src/rng.cpp
)

target_link_libraries(test_rng
  PRIVATE
    dgcolor_project_options
    dgcolor_parlay
)

add_test(
  NAME rng
  COMMAND $<TARGET_FILE:test_rng>
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
    COMMAND $<TARGET_FILE:test_types_batch>
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
    COMMAND $<TARGET_FILE:test_interfaces_compile>
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
    COMMAND $<TARGET_FILE:test_graph_store>
  )

  add_executable(test_validator
    test_validator.cpp
    ../src/graph_store.cpp
    ../src/validator.cpp
  )

  target_link_libraries(test_validator
    PRIVATE
      dgcolor_project_options
      dgcolor_parlay
  )

  add_test(
    NAME validator
    COMMAND $<TARGET_FILE:test_validator>
  )

  add_executable(test_seq_baseline
    test_seq_baseline.cpp
    ../src/graph_store.cpp
    ../src/validator.cpp
    ../src/seq_baseline_engine.cpp
  )

  target_link_libraries(test_seq_baseline
    PRIVATE
      dgcolor_project_options
      dgcolor_parlay
  )

  add_test(
    NAME seq_baseline
    COMMAND $<TARGET_FILE:test_seq_baseline>
  )

  add_executable(test_seq_baseline_fuzz
    test_seq_baseline_fuzz.cpp
    ../src/graph_store.cpp
    ../src/rng.cpp
    ../src/validator.cpp
    ../src/seq_baseline_engine.cpp
  )

  target_link_libraries(test_seq_baseline_fuzz
    PRIVATE
      dgcolor_project_options
      dgcolor_parlay
  )

  add_test(
    NAME seq_baseline_fuzz
    COMMAND $<TARGET_FILE:test_seq_baseline_fuzz>
  )

  add_executable(test_par_relaxed
    test_par_relaxed.cpp
    ../src/graph_store.cpp
    ../src/validator.cpp
    ../src/par_relaxed_engine.cpp
  )

  target_link_libraries(test_par_relaxed
    PRIVATE
      dgcolor_project_options
      dgcolor_parlay
  )

  add_test(
    NAME par_relaxed
    COMMAND $<TARGET_FILE:test_par_relaxed>
  )

  add_executable(test_par_relaxed_fuzz
    test_par_relaxed_fuzz.cpp
    ../src/graph_store.cpp
    ../src/rng.cpp
    ../src/validator.cpp
    ../src/par_relaxed_engine.cpp
  )

  target_link_libraries(test_par_relaxed_fuzz
    PRIVATE
      dgcolor_project_options
      dgcolor_parlay
  )

  add_test(
    NAME par_relaxed_fuzz
    COMMAND $<TARGET_FILE:test_par_relaxed_fuzz>
  )
else()
  message(WARNING "Skipping Parlay-dependent tests (test_types_batch, test_interfaces_compile, test_graph_store, test_validator, test_seq_baseline, test_seq_baseline_fuzz, test_par_relaxed, test_par_relaxed_fuzz) because ParlayLib was not found.")
endif()
