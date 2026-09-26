#!/bin/bash
# Run M2 tests and count passing/failing

cd /workspace
M2_DIR="spec/tests/M2"
COMPILER="./build/farmc"

if [ ! -f "$COMPILER" ]; then
    echo "ERROR: Compiler not found at $COMPILER"
    exit 1
fi

pass_count=0
fail_count=0
passing_tests=()
failing_tests=()

for test_file in $M2_DIR/*.fm; do
    test_name=$(basename "$test_file" .fm)
    expected_file="${test_file%.fm}.expected"
    
    if [ ! -f "$expected_file" ]; then
        continue
    fi
    
    # Parse expected file for kind, exit code, and expected output
    kind=$(grep "^# kind:" "$expected_file" | cut -d: -f2 | xargs)
    expected_exit=$(grep "^# exit:" "$expected_file" | cut -d: -f2 | xargs)
    expected_stdout=$(sed -n '/^# stdout:/,/^# end/p' "$expected_file" | sed '1d;$d')
    
    # Compile
    compile_output=$($COMPILER build "$test_file" -o "/tmp/test_${test_name}" 2>&1)
    compile_result=$?
    
    if [ "$kind" == "compile_error" ]; then
        # Test expects compilation to fail
        if [ $compile_result -ne 0 ]; then
            # TODO: Check actual error messages match
            ((pass_count++))
            passing_tests+=("$test_name")
            echo "PASS: $test_name (compile_error)"
        else
            ((fail_count++))
            failing_tests+=("$test_name")
            echo "FAIL: $test_name (expected compile error, but compiled successfully)"
        fi
    elif [ "$kind" == "run" ] || [ "$kind" == "run_approx" ]; then
        # Test expects successful compilation and run
        if [ $compile_result -ne 0 ]; then
            ((fail_count++))
            failing_tests+=("$test_name")
            echo "FAIL: $test_name (compilation failed)"
        else
            # Run the test
            actual_stdout=$("/tmp/test_${test_name}" 2>&1)
            actual_exit=$?
            
            if [ "$kind" == "run_approx" ]; then
                # For run_approx, compare with tolerance
                epsilon=$(grep "^# epsilon:" "$expected_file" | cut -d: -f2 | xargs)
                if [ -z "$epsilon" ]; then
                    epsilon="1e-15"
                fi
                
                # Simple line-by-line comparison (treating each line as a float)
                match=true
                IFS=$'\n' read -d '' -r -a expected_lines <<< "$expected_stdout"
                IFS=$'\n' read -d '' -r -a actual_lines <<< "$actual_stdout"
                
                if [ "${#expected_lines[@]}" -ne "${#actual_lines[@]}" ]; then
                    match=false
                else
                    for i in "${!expected_lines[@]}"; do
                        exp="${expected_lines[$i]}"
                        act="${actual_lines[$i]}"
                        
                        # Try float comparison with epsilon
                        if ! awk -v a="$act" -v e="$exp" -v eps="$epsilon" 'BEGIN { exit !(a-e < eps && e-a < eps) }' 2>/dev/null; then
                            match=false
                            break
                        fi
                    done
                fi
                
                if [ "$actual_exit" == "$expected_exit" ] && [ "$match" == "true" ]; then
                    ((pass_count++))
                    passing_tests+=("$test_name")
                    echo "PASS: $test_name (approx)"
                else
                    ((fail_count++))
                    failing_tests+=("$test_name")
                    echo "FAIL: $test_name (approx output or exit code mismatch)"
                fi
            else
                # Compare exit code and stdout exactly
                if [ "$actual_exit" == "$expected_exit" ] && [ "$actual_stdout" == "$expected_stdout" ]; then
                    ((pass_count++))
                    passing_tests+=("$test_name")
                    echo "PASS: $test_name"
                else
                    ((fail_count++))
                    failing_tests+=("$test_name")
                    echo "FAIL: $test_name (output or exit code mismatch)"
                fi
            fi
        fi
    elif [ "$kind" == "runtime_trap" ]; then
        # Test expects runtime error
        if [ $compile_result -ne 0 ]; then
            ((fail_count++))
            failing_tests+=("$test_name")
            echo "FAIL: $test_name (compilation failed, expected runtime trap)"
        else
            "/tmp/test_${test_name}" >/dev/null 2>&1
            actual_exit=$?
            if [ $actual_exit -ne 0 ]; then
                ((pass_count++))
                passing_tests+=("$test_name")
                echo "PASS: $test_name (runtime_trap)"
            else
                ((fail_count++))
                failing_tests+=("$test_name")
                echo "FAIL: $test_name (expected runtime trap, but exited 0)"
            fi
        fi
    else
        ((fail_count++))
        failing_tests+=("$test_name")
        echo "FAIL: $test_name (unknown test kind: $kind)"
    fi
done

total=$((pass_count + fail_count))
echo ""
echo "===== M2 Test Results ====="
echo "Passing: $pass_count/$total"
echo "Failing: $fail_count"
echo ""
echo "Passing tests:"
for test in "${passing_tests[@]}"; do
    echo "  - $test"
done
