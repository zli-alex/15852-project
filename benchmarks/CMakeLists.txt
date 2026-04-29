add_executable(bench_smoke
  bench_smoke.cpp
  ../src/graph_store.cpp
  ../src/rng.cpp
  ../src/seq_baseline_engine.cpp
  ../src/validator.cpp
)

target_link_libraries(bench_smoke
  PRIVATE
    dgcolor_project_options
    dgcolor_parlay
)
