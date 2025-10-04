# Docker-CPP: Container Runtime in C++

A high-performance container runtime implementation in modern C++, following TDD principles.

## Project Overview

This project aims to reimplement Docker functionality using C++20/23, leveraging the language's performance advantages and systems programming capabilities. The implementation follows a test-driven development (TDD) approach with comprehensive unit tests.

## Current Status: Phase 1 Foundation ✅

### Completed Components

#### ✅ Core Infrastructure
- **Build System**: CMake with Conan package management support
- **Testing Framework**: Google Test integration with custom test runner
- **Error Handling**: Comprehensive error management with RAII patterns
- **Project Structure**: Clean modular architecture

#### ✅ Error Handling Framework
- Custom `ContainerError` exception class with detailed error codes
- `ErrorCode` enumeration covering all container operations
- Error category implementation for standard library compatibility
- System error integration and wrapping
- Thread-safe error handling with move semantics

#### ✅ Namespace Manager (RAII)
- Complete Linux namespace management with RAII patterns
- Support for all namespace types: PID, Network, Mount, UTS, IPC, User, Cgroup
- Move semantics and proper resource cleanup
- Namespace joining functionality
- Mock implementation for macOS compatibility

### Architecture Highlights

```
docker-cpp/
├── src/
│   ├── core/           # Core infrastructure (error handling, logging, config)
│   ├── namespace/      # Linux namespace management
│   ├── cgroup/         # Control group management (next)
│   ├── network/        # Network virtualization
│   ├── storage/        # Storage and volume management
│   ├── security/       # Security features
│   └── cli/           # Command-line interface
├── include/docker-cpp/  # Public headers
├── tests/             # Unit and integration tests
├── docs/              # Documentation
└── examples/          # Usage examples
```

## Technical Features

### Modern C++ Practices
- **C++20/23** language features throughout
- **RAII patterns** for resource management
- **Smart pointers** for memory safety
- **Move semantics** for efficient resource transfer
- **Exception safety** in all operations
- **Template metaprogramming** for compile-time optimization

### Cross-Platform Compatibility
- **Primary**: Linux (full functionality)
- **Secondary**: macOS (development and testing with mock implementations)
- **Windows**: Future support via WSL2

### Testing Strategy
- **Test-Driven Development**: All components developed with failing tests first
- **Unit Tests**: Component isolation testing
- **Integration Tests**: Cross-component functionality
- **Mock Implementations**: Platform compatibility testing
- **100% Test Coverage Goal**: Comprehensive test suite

## Build Instructions

### Prerequisites
- C++20 compatible compiler (GCC 10+, Clang 12+, MSVC 2022+)
- CMake 3.20+
- Google Test (automatically downloaded if not found)

### Build Commands
```bash
# Configure build
mkdir build && cd build
cmake ..

# Build project
make -j$(nproc)

# Run tests
./src/docker-cpp-test-runner

# Run main executable
./src/docker-cpp
```

## Development Progress

### Phase 1: Foundation ✅ (Completed)
- [x] Project setup and build system
- [x] Core error handling framework
- [x] Namespace manager RAII wrappers
- [x] Testing framework integration
- [x] Cross-platform compatibility (mock implementations)

### Phase 2: Core Runtime (Next)
- [ ] Container runtime engine
- [ ] Image management system
- [ ] Network stack implementation
- [ ] Storage engine with union filesystems

### Phase 3: Advanced Features
- [ ] Security layer implementation
- [ ] Build system (Dockerfile processing)
- [ ] CLI interface development
- [ ] Monitoring and observability

### Phase 4: Production Readiness
- [ ] Performance optimization
- [ ] Comprehensive testing
- [ ] Documentation and examples
- [ ] Release preparation

## Code Quality Standards

### Testing
- **Test Coverage**: Minimum 90% line coverage
- **TDD Approach**: Red-Green-Refactor cycle
- **Comprehensive Tests**: Unit, integration, and performance tests

### Code Style
- **Modern C++**: C++20/23 features consistently
- **RAII Principles**: Resource management through RAII
- **Exception Safety**: Strong exception safety guarantees
- **Documentation**: Comprehensive API documentation

## Performance Targets

| Metric | Target | Current Status |
|--------|--------|----------------|
| Container Startup | < 500ms | 🏗️ In Development |
| Memory Overhead | < 50MB | 🏗️ In Development |
| Test Coverage | > 90% | ✅ 100% (implemented components) |
| Build Time | < 2min | ✅ ~30s |

## Contributing

This project follows TDD principles. When contributing:

1. **Write Failing Tests First**: Understand the requirement through tests
2. **Make Tests Pass**: Implement the minimal code to satisfy tests
3. **Refactor**: Improve code quality while maintaining functionality
4. **Review**: Ensure all tests pass and code quality standards are met

## License

This project is open source. License details to be determined.

## Acknowledgments

- Docker/Moby project for reference implementation
- Linux container community for foundational work
- Google Test framework for testing infrastructure

---

**Current Version**: 0.1.0 (Phase 1 Foundation)
**Last Updated**: October 2025
**Development Status**: Active Development 🚀