#!/usr/bin/env bash

SCRIPTS=test/benchmarks # all scripts location
RESULTS=tmp/results     # dir to save results
COUNT=1
FAILED=0

#BENCHMARK_ALGOS="gzip lz4 snappy zstd"

# Compression levels for each algo: min default max
COMPRESSION_LEVELS="brotli:0 3 11|gzip:1 6 9|deflate:1 6 9|lz4:0 5 16|snappy:.|zstd:1 7 19"
ALGO_THREADS="zstd:1 2 4 8"

ALGOS=$1
[ "a$ALGOS" == "a" ] && ALGOS=$BENCHMARK_ALGOS

# Fail rest of the script at first error
set -e

cleanup() {
    echo "Cleaning up benchmark environment..."
    psql -q -v ON_ERROR_STOP=1 -f ${SCRIPTS}/teardown.sql >/dev/null 2>&1 || true
}
trap cleanup EXIT

get_hash_value() {
    HASH=$1
    KEY=$2

    set -f
    OLD_IFS=$IFS
    IFS='|'
    ret=""

    for item in $HASH; do
        IFS=$OLD_IDS
        key="${item%%:*}"
        val="${item#*:}"
        case "$KEY" in
        *"$key"*)
            ret=$val
            ;;
        esac

        IFS='|'
    done

    IFS=$OLD_IFS
    set +f
    echo "$ret"
}

get_level() {
    HASH=$1
    KEY=$2
    LEVEL=$3

    LEVELS=$(get_hash_value "$HASH" "$KEY")
    read MIN_LEVEL DEFAULT_LEVEL MAX_LEVEL <<EOF
$LEVELS
EOF
    case $LEVEL in
    "MIN")
        echo $MIN_LEVEL
        ;;
    "DEFAULT")
        echo $DEFAULT_LEVEL
        ;;
    "MAX")
        echo $MAX_LEVEL
        ;;
    esac
}

show_separator() {
    TITLE=$1
    SEPARATOR="--------------+----------------------+--------------+-----------"

    if [ "a$TITLE" != "a" ]; then
        REMAINDER=$((4 + ${#TITLE}))
        STRING="${SEPARATOR:0:4}${TITLE}${SEPARATOR:$REMAINDER}"
        SEPARATOR="$STRING"
    fi

    echo $SEPARATOR
}

show_header() {
    show_separator
    echo "Test #/Result | Algorithm / Function | Result, bytes| Duration"
}

prepare_dataset() {
    psql -q -v ON_ERROR_STOP=1 -f ${SCRIPTS}/prepare.sql
}

run_test() {
    ALGO=$1
    LEVEL=$2
    THREADS=$3

    SQL=${SCRIPTS}/compress.sql
    OUT=${RESULTS}/${ALGO}_${LEVEL}.out
    TEST=${ALGO}

    if [ "a$LEVEL" != "a" ]; then
        SQL=${SCRIPTS}/compress_level.sql
        TEST="${TEST}-${LEVEL}"
    fi
    if [ "a$THREADS" != "a" ]; then
        SQL=${SCRIPTS}/compress_threads.sql
        TEST="${TEST}-${THREADS}"
    fi

    if [ -f "$SQL" ]; then
        START_TIME=$(date +%s%3N)

        #echo "$ALGO $LEVEL $THREADS"
        STATUS=0
        psql -q -v ON_ERROR_STOP=1 \
            -v func="${ALGO}" -v level="$LEVEL" -v threads="$THREADS" \
            -f $SQL >$OUT 2>&1 || STATUS=$?

        END_TIME=$(date +%s%3N)
        DURATION=$((END_TIME - START_TIME))

        BYTES=$(grep -A 1 -e "-------" $OUT | tail -n 1)
        if [ $STATUS -eq 0 ]; then
            printf "%3d OK        | %-20s | %'12d | %'6d ms\n" $COUNT "$TEST" $BYTES $DURATION
        else
            printf "%3d ERROR %3d | %-20s | %'12d | %'6d ms\n" $COUNT $STATUS "$TEST" 0 $DURATION
            FAILED=$((FAILED + 1))
        fi

        COUNT=$((COUNT + 1))
    fi

    return $STATUS
}

# =============================== MAIN ========================================
# Validation of algos list
ALGO_LIST="$ALGOS"
ALGOS=""

for ALGO in $ALGO_LIST; do
    case "$BENCHMARK_ALGOS" in
    *"$ALGO"*)
        ALGOS="$ALGOS $ALGO"
        ;;
    *)
        echo "Algorithm '$ALGO' is not supported by current configuration (run ./configure again if necessary)"
        ;;
    esac
done

# Prepare test environment
[ ! -d $RESULTS ] && mkdir $RESULTS
rm -f ${RESULTS}/*

echo "Load testing following algorithms: $ALGOS"
prepare_dataset
show_header

# Running load tests
for ALGO in $ALGOS; do
    show_separator "$ALGO single-threaded"
    LEVELS=$(get_hash_value "$COMPRESSION_LEVELS" "$ALGO")
    # echo "Load testing '$ALGO' with compression levels: $LEVELS"

    for LEVEL in $LEVELS; do
        TEST=${ALGO}_${LEVEL}
        FUNC="${ALGO}"

        [ "$LEVEL" == "." ] && LEVEL=""

        STATUS=0
        run_test $ALGO $LEVEL || STATUS=$?
    done

    THREADS_LIST=$(get_hash_value "$ALGO_THREADS" "$ALGO")
    if [ "a$THREADS_LIST" != "a" ]; then
        # echo "Multi-thread mode tests"
        show_separator "$ALGO multi-threaded"
    fi
    for THREADS in $THREADS_LIST; do
        MIN_LEVEL=$(get_level "$COMPRESSION_LEVELS" "$ALGO" "MIN")
        DEFAULT_LEVEL=$(get_level "$COMPRESSION_LEVELS" "$ALGO" "DEFAULT")
        MAX_LEVEL=$(get_level "$COMPRESSION_LEVELS" "$ALGO" "MAX")
        # echo "Running $ALGO-$DEFAULT_LEVEL test with $THREADS threads"
        run_test $ALGO $MIN_LEVEL $THREADS
        run_test $ALGO $DEFAULT_LEVEL $THREADS
        run_test $ALGO $MAX_LEVEL $THREADS
    done

done

show_separator

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
exit 0
