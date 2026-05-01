BEGIN {
  n = split("host,date,git_commit,parlay_threads,engine_name,workload,seed,num_vertices,delta_cap,batch_size,palette_multiplier,palette_size,max_rounds,updates_requested,updates_generated,updates_applied,updates_rejected,accepted_ratio,batches_generated,batches_applied,batch_accepted_ratio,generation_attempts,generator_same_color_attempts,generator_same_color_chosen,generator_same_color_fallbacks,initial_edges_requested,initial_edges,final_edges,target_accepted,insert_ratio,max_generation_attempts,build_seconds,update_seconds,generation_seconds,engine_apply_seconds,graph_apply_seconds,validate_seconds,validate_final_only,throughput_updates_per_second,max_degree_observed,graph_validated,coloring_validated,vertices_touched_total,total_rounds,fallback_count,repair_calls,repair_rounds,sequential_repair_calls,sequential_repair_rounds,active_vertices_initial_total,active_vertices_expanded_total,conflicted_vertices_initial_total,neighbor_scans,commits_total,repair_seconds,active_build_seconds,internal_validation_seconds,max_active_size,active_size_round_total,neighbor_materializations,direct_neighbor_scans,recolor_calls,recolored_vertices_total,cascade_steps_total,full_fallback_count,level_conflict_choices,active_vertices_total,repair_rounds_total,proposal_count,commit_count,unresolved_count,sequential_fast_path_count,validation_message,color_validation_message", fields, ",")
  for (i = 1; i <= n; ++i) {
    printf "%s%s", (i == 1 ? "" : ","), fields[i]
  }
  printf "\n"
}

function csv(value, escaped) {
  escaped = value
  gsub(/"/, "\"\"", escaped)
  return "\"" escaped "\""
}

function reset_record(    i) {
  for (i = 1; i <= n; ++i) {
    rec[fields[i]] = ""
  }
}

function flush_record(    i, key, value) {
  if (!in_record) {
    return
  }
  rec["host"] = meta["host"]
  rec["date"] = meta["date"]
  rec["git_commit"] = meta["git_commit"]
  rec["parlay_threads"] = meta["parlay_threads"]
  for (i = 1; i <= n; ++i) {
    key = fields[i]
    value = rec[key]
    printf "%s%s", (i == 1 ? "" : ","), csv(value)
  }
  printf "\n"
  reset_record()
  in_record = 0
}

{
  sep = index($0, "=")
  if (sep == 0) {
    next
  }

  key = substr($0, 1, sep - 1)
  value = substr($0, sep + 1)

  if (key == "date" || key == "host" || key == "git_commit" || key == "parlay_threads") {
    meta[key] = value
    next
  }

  if (key == "benchmark_name") {
    flush_record()
    reset_record()
    in_record = 1
    next
  }

  if (in_record) {
    rec[key] = value
  }
}

END {
  flush_record()
}
