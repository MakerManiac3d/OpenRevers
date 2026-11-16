#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Run the local CI before every `git push` (skip once with: git push --no-verify).
set -euo pipefail
cd "$(dirname "$0")/.."
cat > .git/hooks/pre-push <<'HOOK'
#!/usr/bin/env bash
exec "$(git rev-parse --show-toplevel)/tools/ci-local.sh"
HOOK
chmod +x .git/hooks/pre-push tools/ci-local.sh
echo "Hook pre-push instalado."
