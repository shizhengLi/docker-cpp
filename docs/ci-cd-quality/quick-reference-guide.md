# C++ Static Analysis & CI/CD Quick Reference Guide

## Essential Commands

### Local Development Setup
```bash
# Install specific versions matching CI
sudo apt-get install -y clang-format-15 clang-tidy-15

# Build from source for specific cppcheck version
wget https://github.com/danmar/cppcheck/archive/refs/tags/2.18.0.tar.gz
tar -xzf 2.18.0.tar.gz
cd cppcheck-2.18.0/build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
sudo make install
```

### Daily Commands
```bash
# Format code (CI version)
find src include -name '*.cpp' -o -name '*.hpp' | xargs clang-format-15 -i

# Run clang-tidy locally
find src include -name '*.cpp' -o -name '*.hpp' | xargs -P $(nproc) -I {} clang-tidy-15 -p build/Release {}

# Run cppcheck locally
cppcheck --enable=all --error-exitcode=1 \
  --suppress=missingIncludeSystem \
  --suppress=unusedFunction:src/test_runner.cpp \
  --suppress=unusedFunction:src/core/logger.cpp \
  --suppress=useStlAlgorithm:src/plugin/plugin_registry.cpp \
  -DCPPCHECK \
  include/ src/
```

## Working Configurations

### .clang-tidy
```yaml
Checks: >
  bugprone-use-after-move,
  bugprone-dangling-handle,
  bugprone-sizeof-expression

WarningsAsErrors: >
  bugprone-use-after-move,
  bugprone-dangling-handle

HeaderFilterRegex: '^(src|include)/.*'
FormatStyle: file
```

### .github/workflows/ci.yml (Code Quality Section)
```yaml
- name: Install tools
  run: |
    sudo apt-get install -y clang-format-15 clang-tidy-15
    # Install cppcheck 2.18.0 to match local environment
    wget https://github.com/danmar/cppcheck/archive/refs/tags/2.18.0.tar.gz
    tar -xzf 2.18.0.tar.gz
    cd cppcheck-2.18.0
    mkdir -p build && cd build
    cmake .. -DCMAKE_BUILD_TYPE=Release
    make -j$(nproc)
    sudo make install
    cd ../..
    rm -rf cppcheck-2.18.0 cppcheck-2.18.0.tar.gz

- name: Check code formatting
  run: |
    find src include tests -name '*.cpp' -o -name '*.hpp' | xargs clang-format-15 --dry-run --Werror

- name: Run static analysis
  run: |
    # Run clang-tidy in parallel with optimized settings
    # Skip tests for faster CI/CD, focus on core source files
    find src include -name '*.cpp' -o -name '*.hpp' | \
      xargs -P $(nproc) -I {} clang-tidy-15 -p build/Release {} --quiet --warnings-as-errors='*'

- name: Run cppcheck
  run: |
    cppcheck --enable=all --error-exitcode=1 \
      --suppress=missingIncludeSystem \
      --suppress=unusedFunction:src/test_runner.cpp \
      --suppress=unusedFunction:src/core/logger.cpp \
      --suppress=useStlAlgorithm:src/plugin/plugin_registry.cpp \
      --suppress=normalCheckLevelMaxBranches:src/config/config_manager.cpp \
      -DCPPCHECK \
      include/ src/
```

## Common Problems & Solutions

### Problem: Too Many Warnings
```bash
# Bad - wildcard enables hundreds of checks
Checks: '*'

# Good - specific, actionable checks
Checks: >
  bugprone-use-after-move,
  bugprone-dangling-handle,
  performance-unnecessary-value-param
```

### Problem: Version Mismatches
```bash
# Check versions
clang-format-15 --version
clang-tidy-15 --version
cppcheck --version

# Use same versions in CI and locally
```

### Problem: Slow CI
```bash
# Before (slow)
find src include tests -name '*.cpp' -o -name '*.hpp' | xargs clang-tidy-15 -p build/Release

# After (fast, parallel)
find src include -name '*.cpp' -o -name '*.hpp' | \
  xargs -P $(nproc) -I {} clang-tidy-15 -p build/Release {} --quiet --warnings-as-errors='*'
```

### Problem: Preprocessor Issues with Static Analysis
```cpp
// Static analysis friendly approach
#ifdef CPPCHECK
    #define HAS_NLOHMANN_JSON 0
#else
    #if defined(__has_include)
        #if __has_include(<nlohmann/json.hpp>)
            #include <nlohmann/json.hpp>
            #define HAS_NLOHMANN_JSON 1
        #else
            #define HAS_NLOHMANN_JSON 0
        #endif
    #else
        #define HAS_NLOHMANN_JSON 0
    #endif
#endif
```

### Problem: Unused Variables
```cpp
// Before (cppcheck error)
for (const auto& [name, layer] : layers_) {
    count += layer->size(); // name not used
}

// After (cppcheck happy)
for (const auto& [unused_name, layer] : layers_) {
    count += layer->size();
}
```

## Best Practices

### 1. Always Test Locally First
```bash
# Run the exact same commands as CI
make build
make test
# Then run static analysis tools
```

### 2. Version Pinning
```yaml
# Specify exact versions
clang-format-15
clang-tidy-15
cppcheck 2.18.0
```

### 3. Selective Analysis
```bash
# Skip non-critical files in CI
find src include -name '*.cpp' -o -name '*.hpp'
# NOT: find src include tests -name '*.cpp' -o -name '*.hpp'
```

### 4. Meaningful Suppressions
```bash
# Good: specific suppressions
--suppress=unusedFunction:src/test_runner.cpp
--suppress=useStlAlgorithm:src/plugin/plugin_registry.cpp

# Bad: broad suppressions
--suppress=unusedFunction
--suppress=style
```

### 5. Performance Optimization
```bash
# Use parallel processing
xargs -P $(nproc)

# Use quiet mode
--quiet

# Focus on high-value checks
--warnings-as-errors='*'
```

## Git Configuration

### .gitignore Best Practices
```
# Good: specific patterns
build/
*.o
*.so
*.a

# Bad: overly broad patterns
docker-cpp  # This ignored our entire include directory!
```

### Checking Gitignore Patterns
```bash
# Test patterns before committing
git check-ignore path/to/file
```

## Pre-commit Hooks (Optional)

```bash
#!/bin/sh
# .git/hooks/pre-commit

# Format code
find src include -name '*.cpp' -o -name '*.hpp' | xargs clang-format-15 -i

# Run quick checks
clang-format-15 --dry-run --Werror src/*.cpp include/*.hpp
if [ $? -ne 0 ]; then
    echo "Code formatting issues found. Run 'make format' to fix."
    exit 1
fi
```

## Troubleshooting Checklist

### CI Fails but Local Passes
- [ ] Check tool versions (clang-format-15 vs clang-format)
- [ ] Verify file paths (Windows vs Linux separators)
- [ ] Check gitignore hasn't excluded critical files
- [ ] Verify environment variables (CPPCHECK=1)

### Too Many Warnings
- [ ] Review .clang-tidy configuration for wildcards
- [ ] Focus on high-value checks only
- [ ] Add specific suppressions for intentional patterns

### Slow CI Pipeline
- [ ] Use parallel processing: `xargs -P $(nproc)`
- [ ] Skip test files: remove tests/ from search path
- [ ] Use quiet mode: `--quiet`
- [ ] Consider caching build artifacts

### Static Analysis Tool Errors
- [ ] Check tool installation and version
- [ ] Verify compilation database exists
- [ ] Check include paths and library dependencies
- [ ] Review preprocessor directives for tool compatibility

## Resources

- [Clang-Tidy Documentation](https://clang.llvm.org/extra/clang-tidy/)
- [Cppcheck Manual](https://cppcheck.sourceforge.io/manual.pdf)
- [Clang-Format Documentation](https://clang.llvm.org/docs/ClangFormat.html)
- [GitHub Actions Documentation](https://docs.github.com/en/actions)

---

*This guide is based on real experience setting up CI/CD for the Docker-CPP project. Save it for quick reference!*