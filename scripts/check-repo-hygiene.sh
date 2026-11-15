#!/usr/bin/env bash
set -Eeuo pipefail

fail() {
  printf '[repo-hygiene] FAIL: %s\n' "$*" >&2
  exit 1
}

command -v git >/dev/null 2>&1 || fail "git is required"
tracked="$(git ls-files)" || fail "git ls-files failed; run from repository root"
forbidden="$(printf '%s\n' "$tracked" | grep -E '^(desknx-release\.zip|\.vite/|node_modules/|dist/|SdOut/)|(^|/)\.cache/' || true)"
[[ -z "$forbidden" ]] || fail "generated/local files are tracked:\n$forbidden"

[[ -f .gitignore ]] || fail ".gitignore is missing"
if grep -Eq '^[[:space:]]*\{"content":' .gitignore; then
  fail ".gitignore contains a serialized response envelope, not ignore rules"
fi
for rule in ".vite/" "*.zip" "node_modules/" "dist/"; do
  grep -Fxq "$rule" .gitignore || fail ".gitignore is missing required rule: $rule"
done

printf '%s\n' "[repo-hygiene] OK: no generated Vite cache/release bundle is tracked; .gitignore is clean."
