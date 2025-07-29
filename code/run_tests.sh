#!/bin/bash

# Compile the string library and unit tests
echo "Compiling string library and unit tests..."

# Compile with debugging symbols and warnings
g++ -g -Wall -Wextra -o string_tests strings.cpp units_strings.cpp

# Check if compilation was successful
if [ $? -eq 0 ]; then
    echo "Compilation successful!"
    echo "Running tests..."
    echo "================================"
    
    # Run the tests
    ./string_tests
    
    # Capture the exit code
    TEST_RESULT=$?
    
    # Clean up the executable
    rm -f string_tests
    
    # Exit with the test result code
    exit $TEST_RESULT
else
    echo "Compilation failed!"
    exit 1
fi