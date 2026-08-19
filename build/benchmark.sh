#!/usr/bin/env bash

SCRIPTS=test/benchmarks # all scripts location
RESULTS=tmp/results     # dir to save results
COUNT=1
FAILED=0

# Fail rest of the script at first error
set -e

cleanup() {
    echo "Cleaning up benchmark environment..."
    psql -q -v ON_ERROR_STOP=1 -f ${SCRIPTS}/teardown.sql >/dev/null 2>&1 || true
}
trap cleanup EXIT

# Prepare test environment
[ ! -d $RESULTS ] && mkdir $RESULTS
rm -f ${RESULTS}/*
psql -q -v ON_ERROR_STOP=1 -f ${SCRIPTS}/prepare.sql

echo "--------------+----------------------+--------------+-----------"
echo "Test #/Result | Algorithm / Function | Result, bytes| Duration"
for ALGO in $BENCHMARK_ALGOS; do
    echo "--------------+----------------------+--------------+-----------"
    for PHASE in save compress decompress; do
        TEST=${ALGO}_${PHASE}
        FUNC="${ALGO}"
        case "${PHASE}" in
        compress | save)
            true
            ;;
        decompress)
            [ "$ALGO" == "deflate" ] && FUNC="inflate"
            ;;
        *)
            echo "!!! Unknown benchmark phase ${PHASE}"
            continue
            ;;
        esac

        SQL=${SCRIPTS}/${PHASE}.sql
        OUT=${RESULTS}/${PHASE}.out

        if [ -f "$SQL" ]; then
            START_TIME=$(date +%s%3N)

            STATUS=0
            psql -q -v ON_ERROR_STOP=1 -v func="${FUNC}" -f $SQL >$OUT 2>&1 || STATUS=$?

            [ "$PHASE" == "save" ] && continue

            END_TIME=$(date +%s%3N)
            DURATION=$((END_TIME - START_TIME))

            BYTES=$(sed -n '3p' $OUT)
            if [ $STATUS -eq 0 ]; then
                printf "%3d OK        | %-20s | %'12d | %'6d ms\n" ${COUNT} "${ALGO} ${PHASE}" ${BYTES} ${DURATION}
            else
                printf "%3d ERROR %3d | %-20s | %'12d | %'6d ms\n" ${COUNT} ${STATUS} "${ALGO} ${PHASE}" 0 ${DURATION}
                FAILED=$((FAILED + 1))
            fi

            COUNT=$((COUNT + 1))
        fi
    done
done
echo "--------------+----------------------+--------------+-----------"

TOTAL=$((COUNT - 1))
echo "1..${TOTAL}"
if [ ${FAILED} -eq 0 ]; then
    echo "# All ${TOTAL} benchmarks passed."
    EXIT_CODE=0
else
    echo "# ${FAILED} out of ${TOTAL} benchmarks FAILED."
    echo "Check output files in ${RESULTS} dir"
    EXIT_CODE=1
fi

exit ${EXIT_CODE}
