#!/usr/bin/env python3
"""
Pre-build patch for M5StamPLC INA226 API compatibility with M5Unified 0.2.x

This script patches the M5StamPLC library to use the correct INA226 API calls
that are compatible with M5Unified 0.2.x. This is a temporary workaround until
upstream M5StamPLC releases an updated version.

Usage: Called automatically by PlatformIO during build process
"""

import os
import sys
from pathlib import Path

def patch_m5stamplc_ina226():
    """Patch M5StamPLC INA226 API calls for M5Unified 0.2.x compatibility"""
    
    # Get the M5StamPLC library path
    lib_path = Path(".pio/libdeps/m5stack-stamps3-freertos/M5StamPLC")
    
    if not lib_path.exists():
        print("M5StamPLC library not found, skipping patch")
        return
    
    # Find all .cpp and .h files that might need patching
    files_to_patch = []
    for ext in ["*.cpp", "*.h"]:
        files_to_patch.extend(lib_path.rglob(ext))
    
    patches_applied = 0
    
    for file_path in files_to_patch:
        try:
            with open(file_path, 'r', encoding='utf-8') as f:
                content = f.read()
            
            original_content = content
            
            # Patch INA226 API calls
            # Old: ina226.begin()
            # New: ina226.begin(&Wire)
            content = content.replace(
                "ina226.begin();",
                "ina226.begin(&Wire);"
            )
            
            # Patch other potential INA226 calls
            content = content.replace(
                "ina226.begin(I2C_SDA, I2C_SCL);",
                "ina226.begin(&Wire, I2C_SDA, I2C_SCL);"
            )
            
            # Only write if content changed
            if content != original_content:
                with open(file_path, 'w', encoding='utf-8') as f:
                    f.write(content)
                patches_applied += 1
                print(f"Patched: {file_path}")
                
        except Exception as e:
            print(f"Error patching {file_path}: {e}")
    
    if patches_applied > 0:
        print(f"M5StamPLC INA226 patch: {patches_applied} files updated")
    else:
        print("M5StamPLC INA226 patch: No files needed updating")

if __name__ == "__main__":
    patch_m5stamplc_ina226()
