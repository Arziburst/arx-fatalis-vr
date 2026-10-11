#!/usr/bin/env bash
# Status line for coordinator machines (macOS / Linux; Windows: statusline.ps1). Prints
#   Opus 5.5 (high) · ctx 837k/1M (84%) · 5h 14% · week 92%
# — model (effort) · context used in this chat · 5-hour and weekly limits (claude.ai Pro / Max only) —
# and saves Claude Code's status JSON to ~/.claude/coordinator/statusline.json for the coordinator.
input="$(cat)"
dir="$HOME/.claude/coordinator"
mkdir -p "$dir" 2>/dev/null && printf '%s' "$input" > "$dir/statusline.json"
if command -v jq >/dev/null 2>&1; then
  printf '%s' "$input" | jq -r '
    def tok: if . >= 1000000 then "\((. / 100000 | floor) / 10)M"
             elif . >= 1000 then "\(. / 1000 | floor)k" else "\(floor)" end;
    [ ((.model.display_name // "Claude") + (if .effort.level then " (\(.effort.level))" else "" end)),
      (.context_window | select(type == "object" and .used_percentage != null) |
        "ctx \(((.current_usage.input_tokens // 0) + (.current_usage.cache_read_input_tokens // 0)
               + (.current_usage.cache_creation_input_tokens // 0)) | tok)/\((.context_window_size // 0) | tok) (\(.used_percentage | floor)%)"),
      (.rate_limits.five_hour | select(type == "object" and .used_percentage != null) | "5h \(.used_percentage | floor)%"),
      (.rate_limits.seven_day | select(type == "object" and .used_percentage != null) | "week \(.used_percentage | floor)%")
    ] | join(" · ")' 2>/dev/null || echo "Claude"
else
  echo "Claude · install jq for the full status line"
fi
