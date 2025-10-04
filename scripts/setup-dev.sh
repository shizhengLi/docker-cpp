#!/bin/bash

# Docker-CPP Development Setup Script
# This script sets up the development environment for the Docker-CPP project

set -e  # Exit on any error

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Function to print colored output
print_status() {
    echo -e "${BLUE}[INFO]${NC} $1"
}

print_success() {
    echo -e "${GREEN}[SUCCESS]${NC} $1"
}

print_warning() {
    echo -e "${YELLOW}[WARNING]${NC} $1"
}

print_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

# Check if we're in the right directory
if [ ! -f "CMakeLists.txt" ] || [ ! -d "src" ]; then
    print_error "Please run this script from the docker-cpp project root directory"
    exit 1
fi

print_status "Setting up Docker-CPP development environment..."

# Check for required tools
check_required_tools() {
    print_status "Checking for required tools..."

    # Check CMake
    if ! command -v cmake &> /dev/null; then
        print_error "CMake is not installed. Please install CMake 3.20 or later."
        exit 1
    fi

    CMAKE_VERSION=$(cmake --version | head -n1 | awk '{print $3}')
    print_success "CMake version: $CMAKE_VERSION"

    # Check Python (for Conan)
    if ! command -v python3 &> /dev/null; then
        print_error "Python 3 is not installed. Please install Python 3.8 or later."
        exit 1
    fi

    PYTHON_VERSION=$(python3 --version | awk '{print $2}')
    print_success "Python version: $PYTHON_VERSION"

    # Check for compiler
    if command -v g++ &> /dev/null; then
        GCC_VERSION=$(g++ --version | head -n1 | awk '{print $4}')
        print_success "GCC version: $GCC_VERSION"
    elif command -v clang++ &> /dev/null; then
        CLANG_VERSION=$(clang++ --version | head -n1 | awk '{print $3}')
        print_success "Clang version: $CLANG_VERSION"
    else
        print_error "No C++ compiler found. Please install GCC 10+ or Clang 12+"
        exit 1
    fi
}

# Install Conan if not present
setup_conan() {
    print_status "Setting up Conan package manager..."

    if ! command -v conan &> /dev/null; then
        print_status "Conan not found. Installing Conan 2.0..."
        pip3 install "conan>=2.0.0,<3.0.0"

        if command -v conan &> /dev/null; then
            print_success "Conan installed successfully"
            CONAN_VERSION=$(conan --version | head -n1)
            print_success "Conan version: $CONAN_VERSION"
        else
            print_error "Failed to install Conan"
            exit 1
        fi
    else
        CONAN_VERSION=$(conan --version | head -n1)
        print_success "Conan already installed: $CONAN_VERSION"
    fi

    # Configure Conan
    print_status "Configuring Conan..."
    conan profile detect --force

    # Enable Conan v2 mode
    export CONAN_V2_MODE=1
    echo 'export CONAN_V2_MODE=1' >> ~/.bashrc
}

# Setup development dependencies
setup_dependencies() {
    print_status "Installing development dependencies..."

    # Install dependencies with Conan
    if [ -f "conanfile.txt" ]; then
        print_status "Installing dependencies from conanfile.txt..."

        # Detect platform and create appropriate profile
        OS=$(uname -s)
        ARCH=$(uname -m)

        case $OS in
            Linux)
                CONAN_OS=Linux
                ;;
            Darwin)
                CONAN_OS=Macos
                ;;
            *)
                print_error "Unsupported operating system: $OS"
                exit 1
                ;;
        esac

        case $ARCH in
            x86_64)
                CONAN_ARCH=x86_64
                ;;
            arm64|aarch64)
                CONAN_ARCH=armv8
                ;;
            *)
                print_error "Unsupported architecture: $ARCH"
                exit 1
                ;;
        esac

        # Create development profile
        cat > conan_dev_profile << EOF
[settings]
os=$CONAN_OS
arch=$CONAN_ARCH
compiler=gcc
compiler.version=11
compiler.cppstd=20
build_type=Release

[options]
gtest:shared=True
fmt:shared=True
spdlog:shared=True
nlohmann_json:shared=False

[conf]
tools.cmake.cmaketoolchain:generator=Ninja
EOF

        print_status "Installing dependencies..."
        conan install . --build=missing --profile conan_dev_profile

        print_success "Dependencies installed successfully"
    else
        print_warning "conanfile.txt not found. Skipping dependency installation."
    fi
}

# Setup build directory
setup_build() {
    print_status "Setting up build directory..."

    if [ -d ".build" ]; then
        print_status "Cleaning existing build directory..."
        rm -rf .build
    fi

    # Create build directory
    mkdir -p .build

    print_status "Configuring project with CMake..."

    # Use Conan-generated preset if available
    if [ -f ".build/conan_toolchain.cmake" ]; then
        cmake --preset conan-release -DCMAKE_BUILD_TYPE=Release
    else
        cmake .. -DCMAKE_BUILD_TYPE=Release
    fi

    print_success "Project configured successfully"
}

# Build the project
build_project() {
    print_status "Building project..."

    cd build

    # Get number of CPU cores for parallel build
    case $(uname -s) in
        Linux*)   NPROC=$(nproc);;
        Darwin*)  NPROC=$(sysctl -n hw.ncpu);;
        *)        NPROC=4;;
    esac

    cmake --build . --parallel $NPROC

    print_success "Project built successfully"
}

# Run tests
run_tests() {
    print_status "Running tests..."

    cd build

    if [ -f "tests/tests-core" ] || [ -f "tests/tests-namespace" ] || [ -f "tests/tests-cgroup" ]; then
        ctest --output-on-failure --parallel $NPROC
        print_success "All tests passed"
    else
        print_warning "No test executables found. Skipping tests."
    fi
}

# Setup git hooks
setup_git_hooks() {
    print_status "Setting up git hooks..."

    mkdir -p .git/hooks

    # Pre-commit hook for code formatting
    cat > .git/hooks/pre-commit << 'EOF'
#!/bin/bash

# Pre-commit hook for Docker-CPP
# Runs clang-format and clang-tidy before committing

echo "Running pre-commit checks..."

# Check code formatting
echo "Checking code formatting..."
if command -v clang-format &> /dev/null; then
    FORMATTED_FILES=$(git diff --cached --name-only --diff-filter=ACM | grep -E '\.(cpp|h|hpp)$' || true)
    if [ -n "$FORMATTED_FILES" ]; then
        echo "$FORMATTED_FILES" | xargs clang-format --dry-run --Werror
        if [ $? -ne 0 ]; then
            echo "Code formatting issues found. Please run 'clang-format -i' on the files above."
            exit 1
        fi
    fi
fi

# Run clang-tidy on changed files
echo "Running static analysis..."
if command -v clang-tidy &> /dev/null; then
    CHANGED_FILES=$(git diff --cached --name-only --diff-filter=ACM | grep -E '\.(cpp|h|hpp)$' || true)
    if [ -n "$CHANGED_FILES" ]; then
        echo "$CHANGED_FILES" | xargs clang-tidy --warnings-as-errors='*'
        if [ $? -ne 0 ]; then
            echo "Static analysis issues found. Please fix them before committing."
            exit 1
        fi
    fi
fi

echo "Pre-commit checks passed."
EOF

    chmod +x .git/hooks/pre-commit

    print_success "Git hooks installed"
}

# Create development scripts
create_dev_scripts() {
    print_status "Creating development helper scripts..."

    # Create build script
    cat > scripts/build.sh << 'EOF'
#!/bin/bash
cd build
cmake --build . --parallel $(nproc 2>/dev/null || sysctl -n hw.ncpu || echo 4)
EOF
    chmod +x scripts/build.sh

    # Create test script
    cat > scripts/test.sh << 'EOF'
#!/bin/bash
cd build
ctest --output-on-failure --parallel $(nproc 2>/dev/null || sysctl -n hw.ncpu || echo 4)
EOF
    chmod +x scripts/test.sh

    # Create clean script
    cat > scripts/clean.sh << 'EOF'
#!/bin/bash
echo "Cleaning build directory..."
rm -rf build
echo "Build directory cleaned."
EOF
    chmod +x scripts/clean.sh

    # Create format script
    cat > scripts/format.sh << 'EOF'
#!/bin/bash
echo "Formatting code..."
find . -name '*.cpp' -o -name '*.hpp' | xargs clang-format -i
echo "Code formatted."
EOF
    chmod +x scripts/format.sh

    print_success "Development scripts created"
}

# Main execution
main() {
    print_status "Starting Docker-CPP development setup..."

    check_required_tools
    setup_conan
    setup_dependencies
    setup_build
    build_project
    run_tests
    setup_git_hooks
    create_dev_scripts

    print_success "Development environment setup complete!"
    print_status "You can now:"
    echo "  - Build with: ./scripts/build.sh"
    echo "  - Test with: ./scripts/test.sh"
    echo "  - Clean with: ./scripts/clean.sh"
    echo "  - Format code with: ./scripts/format.sh"
    echo "  - Run all checks with: make check (if available)"

    print_status "Happy coding! 🚀"
}

# Run main function
main "$@"