# experiment 64

## Tests

Always run tests after making changes to the codebase.
To run the tests, use the following command:

```bash
make tests
```

`make tests` will automatically clean up any previous test artifacts and build the necessary components before executing
the tests.
The tests run with a timeout of 10 seconds to prevent hanging. If you see that a timeout has occurred, it means the last
test did not complete successfully within the allotted time.
To know the tests completed, you need to see either "ALL TESTS PASSED" or "SOME TESTS FAILED" messages at the end.

Always add new tests for every new feature/bug fix, if possible.

## Checks

To ensure code quality and consistency, run the following checks:

```bash
make checks
```

This command will run formatting checks, linting, and static analysis on the codebase. Run it after making changes to
ensure everything adheres to the project's coding standards.

## Running

To actually run the OS inside QEMU, use the following command:

```bash
make run
```

### KASAN (shadow memory) mode

A lightweight KASAN-style shadow is available for debugging memory bugs. To run tests with KASAN enabled:

```bash
make tests-kasan
```

See `docs/kasan.md` for how it works, coverage, and limitations.
