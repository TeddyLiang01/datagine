# Development

Datagine should read like a production-oriented C++ systems project. The code
should favor clarity, deterministic behavior, and measurable performance over
premature abstraction.

## Build And Test

```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build
```

Use release builds for benchmark work:

```sh
cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release
cmake --build build-release
```

## C++ Style

- Use C++20.
- Keep public APIs explicit and small.
- Prefer strongly typed domain wrappers at system boundaries.
- Prefer readable code over clever templates.
- Avoid static global state.
- Add comments only where they clarify non-obvious behavior.

## Formatting

The repository does not introduce a formatter dependency in the initial
foundation. Use conventional modern C++ formatting:

- 4-space indentation.
- Braces on the same line for functions and control flow.
- `snake_case` for functions and variables.
- `PascalCase` for domain types and classes.
- `kConstantName` only for constants that need names in local scope.

## Hot Path Expectations

The initial skeleton does not contain performance-critical internals yet. Once
the order book and replay engine are implemented:

- Avoid allocations in book update paths.
- Avoid exceptions in book update paths.
- Keep parsing convenience outside the book update API.
- Validate invariants in tests and configurable replay checks, not by adding
  unnecessary work to every benchmark path.
