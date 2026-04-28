add_executable(bench_smoke
  bench_smoke.cpp
)

target_link_libraries(bench_smoke
  PRIVATE
    dgcolor_project_options
    dgcolor_parlay
)
