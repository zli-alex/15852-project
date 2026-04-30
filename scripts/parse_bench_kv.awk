BEGIN {
  n = split("host,date,git_commit,parlay_threads,engine_name,workload,seed,num_vertices,delta_cap,batch_size,palette_multiplier,updates_generated,updates_applied,accepted_ratio,update_seconds,throughput_updates_per_second,repair_seconds,repair_calls,sequential_repair_calls,sequential_repair_rounds,total_rounds,fallback_count,vertices_touched_total,neighbor_scans,commits_total,graph_validated,coloring_validated", fields, ",")
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
