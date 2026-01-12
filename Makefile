# ============================================================================
# StampPLC CatM+GNSS FreeRTOS - Makefile for Easy Automation
# ============================================================================
# Quick commands for development workflow
# ============================================================================

.PHONY: help build flash monitor clean all

# Default target
help:
	@echo "StampPLC CatM+GNSS FreeRTOS - Available Commands:"
	@echo ""
	@echo "  make build     - Build firmware only"
	@echo "  make flash     - Flash existing firmware to hardware"
	@echo "  make monitor   - Start serial monitor"
	@echo "  make clean     - Clean build artifacts"
	@echo "  make all       - Build, flash, and monitor (full automation)"
	@echo "  make help      - Show this help message"
	@echo ""

# Build firmware
build:
	@echo "🔨 Building firmware..."
	pio run
	@echo "✅ Build complete!"

# Flash firmware to hardware
flash:
	@echo "🚀 Flashing firmware to hardware..."
	pio run --target upload
	@echo "✅ Flash complete!"

# Start serial monitor
monitor:
	@echo "📱 Starting serial monitor..."
	pio device monitor

# Clean build artifacts
clean:
	@echo "🧹 Cleaning build artifacts..."
	pio run --target clean
	@echo "✅ Clean complete!"

# Full automation: build, flash, and monitor
all: build flash
	@echo "🎉 Full automation complete!"
	@echo "Starting serial monitor in 3 seconds..."
	@sleep 3
	@echo "📱 Starting serial monitor..."
	pio device monitor

# Quick development cycle (build + flash)
dev: build flash
	@echo "🚀 Development cycle complete!"
