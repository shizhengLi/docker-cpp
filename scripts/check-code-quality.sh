#!/bin/bash

# Code quality check script for docker-cpp
# This script simulates CI/CD code quality checks locally

set -e  # Exit on any error

echo "🔍 Running code quality checks for docker-cpp..."
echo "=================================================="

# Check if we're in the right directory
if [ ! -f "CMakeLists.txt" ]; then
    echo "❌ Error: Please run this script from the project root directory"
    exit 1
fi

# Install required tools if not available
check_tool() {
    if ! command -v "$1" &> /dev/null; then
        echo "⚠️  $1 not found. Installing..."
        if [[ "$OSTYPE" == "darwin"* ]]; then
            brew install "$1"
        else
            sudo apt-get update && sudo apt-get install -y "$1"
        fi
    fi
}

# For macOS users, we'll skip clang-tidy if not available
# since it's not included in Xcode command line tools by default
if [[ "$OSTYPE" == "darwin"* ]]; then
    if ! command -v clang-tidy &> /dev/null; then
        echo "⚠️  clang-tidy not found on macOS."
        echo "   To install: brew install llvm && export PATH=/opt/homebrew/opt/llvm/bin:\$PATH"
        echo "   Skipping clang-tidy checks for now..."
        SKIP_CLANG_TIDY=true
    fi
fi

echo ""
echo "📝 Step 1: Checking code formatting..."
echo "--------------------------------------"

# Check if clang-format is available
if command -v clang-format &> /dev/null; then
    # Try to match CI/CD version as closely as possible
    CLANG_FORMAT_VERSION=$(clang-format --version | grep -oE 'clang-format version [0-9]+' | grep -oE '[0-9]+')
    echo "Using clang-format version: $CLANG_FORMAT_VERSION"

    # Run format check
    if find src include tests -name '*.cpp' -o -name '*.hpp' | xargs clang-format --dry-run --Werror; then
        echo "✅ Code formatting check passed"
    else
        echo "❌ Code formatting issues found. Run:"
        echo "   find src include tests -name '*.cpp' -o -name '*.hpp' | xargs clang-format -i"
        exit 1
    fi
else
    echo "⚠️  clang-format not found, skipping formatting check"
fi

echo ""
echo "🔍 Step 2: Building project..."
echo "----------------------------"

# Build the project first
if [ -d "build/Release" ]; then
    cd build/Release
    cmake --build . --parallel
    cd ../..
else
    echo "❌ Build directory not found. Please run the build first:"
    echo "   mkdir -p build && cd build && cmake .. && make"
    exit 1
fi

echo "✅ Build completed successfully"

echo ""
echo "🧠 Step 3: Running static analysis..."
echo "------------------------------------"

if [ "${SKIP_CLANG_TIDY}" = true ]; then
    echo "⏭️  Skipping clang-tidy checks (not available)"
else
    # Run clang-tidy with our configuration
    if find src include tests -name '*.cpp' -o -name '*.hpp' | xargs clang-tidy -p build/Release; then
        echo "✅ Static analysis completed successfully"
    else
        echo "❌ Static analysis found issues"
        exit 1
    fi
fi

echo ""
echo "🧪 Step 4: Running tests..."
echo "--------------------------"

cd build/Release
if ./tests/tests-core && ./tests/tests-namespace && ./tests/tests-cgroup && ./tests/tests-plugin && ./tests/tests-config && ./tests/tests-integration; then
    echo "✅ All tests passed"
else
    echo "❌ Some tests failed"
    exit 1
fi
cd ../..

echo ""
echo "🎉 All code quality checks passed! 🎉"
echo "=================================================="
echo "Your code is ready for CI/CD submission."