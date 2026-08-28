#!/usr/bin/env bash
# run_scripted.sh -- drive the instrument_harness under Xvfb with the
# given fixture script. Mirrors the cwd `make run` uses so resources/
# and data/ resolve correctly. Exits non-zero on any FAIL.
#
# Usage: run_scripted.sh [fixture]     (default: fixtures/add_route_delete.txt)
set -euo pipefail

FIXTURE="${1:-fixtures/add_route_delete.txt}"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
FIXTURE_PATH="${FIXTURE}"

# Allow absolute paths and paths relative to project root.
if [[ "${FIXTURE}" != /* ]]; then
	if [[ -f "${SCRIPT_DIR}/${FIXTURE}" ]]; then
		FIXTURE_PATH="${SCRIPT_DIR}/${FIXTURE}"
	else
		FIXTURE_PATH="${PROJECT_ROOT}/${FIXTURE}"
	fi
fi

if [[ ! -f "${FIXTURE_PATH}" ]]; then
	echo "FAIL fixture not found: ${FIXTURE_PATH}" >&2
	exit 1
fi

BIN="${PROJECT_ROOT}/bin"
if [[ ! -x "${BIN}/instrument_harness" ]]; then
	echo "FAIL instrument_harness not built at ${BIN}/instrument_harness" >&2
	exit 1
fi

cd "${BIN}"
# The preset save/load fixture writes a real preset into the shipped preset
# dir; remove it before AND after so the fixture stays deterministic and
# the dir is left clean. (sanitizePresetFilename preserves case.)
rm -f data/instrument_presets/Xm1.ipb data/instrument_presets/xm1.ipb
set +e
xvfb-run -a -s "-screen 0 1280x800x24" \
	"${BIN}/instrument_harness" --script "${FIXTURE_PATH}"
HARNESS_RC=$?
set -e
rm -f data/instrument_presets/Xm1.ipb data/instrument_presets/xm1.ipb
exit ${HARNESS_RC}
