#!/usr/bin/env bash
# soupOS headless smoke test, "the oracle": the gate every commit must pass.
#
# Since v0.30.2 the gate is scripts/gate.sh, eight segments under scripts/gate/
# that each boot their own QEMU and run at once, so it takes as long as the
# slowest segment (about 100 s) instead of one five-minute drive. This file
# stays as the name everything calls; it just hands over.
#
#   ./scripts/smoke-test.sh            # run the gate
#   KEEP=1 ./scripts/smoke-test.sh     # keep every segment's serial log
exec "$(dirname "$0")/gate.sh" "$@"
