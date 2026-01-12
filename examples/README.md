# Examples Directory

This directory contains example code and test programs for the StampPLC project.

## Adding New Examples

When adding new example files:

1. **Use descriptive names** (e.g., `sensor_test.cpp`, `network_demo.cpp`).
2. **Keep examples self-contained** so they do not collide with `src/main.cpp`.
3. **Document usage instructions** in the file header and summarize them here.
4. **List the expected output** so others can verify behaviour quickly.
5. **Test on actual hardware** before committing.

## Best Practices

- **Isolate examples** from the production firmware.
- **Guard entry points** with `#ifdef` or provide alternative `main()` implementations.
- **Keep dependencies minimal**; prefer the stock M5Unified + M5GFX stack.
- **Describe cleanup steps** so developers can revert to the main firmware easily.
