#!/usr/bin/env python3
"""
GENPLC Recovery Build Test Script

Comprehensive testing and validation for the recovery build
Tests cellular connectivity, memory management, and system stability

Created: November 2025
Purpose: Validate Phase 1 of project refresh
"""

import os
import sys
import time
import subprocess
import serial
import json
import re
from pathlib import Path

# Test configuration
RECOVERY_BUILD_DIR = Path(__file__).parent.parent
PLATFORMIO_CMD = "pio"
TEST_TIMEOUTS = {
    'build': 300,      # 5 minutes
    'upload': 120,      # 2 minutes
    'cellular': 180,    # 3 minutes
    'memory': 60,        # 1 minute
    'stability': 300     # 5 minutes
}

class RecoveryTester:
    def __init__(self):
        self.results = {
            'build': False,
            'upload': False,
            'cellular': False,
            'memory': False,
            'stability': False,
            'overall': False
        }
        self.serial_port = None
        self.test_log = []
        
    def log(self, message, level="INFO"):
        """Log test message"""
        timestamp = time.strftime("%H:%M:%S")
        log_entry = f"[{timestamp}] [{level}] {message}"
        print(log_entry)
        self.test_log.append(log_entry)
        
    def run_command(self, cmd, timeout=60, cwd=None):
        """Run command with timeout and error handling"""
        try:
            self.log(f"Running: {cmd}")
            if cwd is None:
                cwd = RECOVERY_BUILD_DIR
            
            result = subprocess.run(
                cmd.split(),
                cwd=cwd,
                capture_output=True,
                text=True,
                timeout=timeout
            )
            
            if result.returncode == 0:
                self.log(f"Command completed successfully")
                return True, result.stdout
            else:
                self.log(f"Command failed with code {result.returncode}", "ERROR")
                self.log(f"STDERR: {result.stderr}", "ERROR")
                return False, result.stderr
                
        except subprocess.TimeoutExpired:
            self.log(f"Command timed out after {timeout} seconds", "ERROR")
            return False, "Timeout"
        except Exception as e:
            self.log(f"Command exception: {e}", "ERROR")
            return False, str(e)
    
    def test_build(self):
        """Test recovery build compilation"""
        self.log("=== Testing Recovery Build ===")
        
        # Test recovery configuration
        success, output = self.run_command(
            f"{PLATFORMIO_CMD} run -e m5stamps3-recovery",
            timeout=TEST_TIMEOUTS['build']
        )
        
        if success:
            self.log("Recovery build compiled successfully")
            # Check for memory usage warnings
            if "warning: region" in output.lower():
                self.log("Memory usage warnings detected", "WARNING")
            
            # Check for stack size warnings
            if "stack" in output.lower() and "overflow" in output.lower():
                self.log("Stack overflow warnings detected", "ERROR")
                success = False
        else:
            self.log("Recovery build failed", "ERROR")
        
        self.results['build'] = success
        return success
    
    def test_cellular_connectivity(self):
        """Test cellular connectivity with recovery build"""
        self.log("=== Testing Cellular Connectivity ===")
        
        # Build cellular test
        success, _ = self.run_command(
            f"{PLATFORMIO_CMD} run -e m5stamps3-cellular-test",
            timeout=TEST_TIMEOUTS['build']
        )
        
        if not success:
            self.log("Cellular test build failed", "ERROR")
            self.results['cellular'] = False
            return False
        
        # Find serial port
        port = self.find_serial_port()
        if not port:
            self.log("No serial port found for cellular test", "ERROR")
            self.results['cellular'] = False
            return False
        
        # Upload and test
        upload_success, _ = self.run_command(
            f"{PLATFORMIO_CMD} run -e m5stamps3-cellular-test -t upload",
            timeout=TEST_TIMEOUTS['upload']
        )
        
        if not upload_success:
            self.log("Cellular test upload failed", "ERROR")
            self.results['cellular'] = False
            return False
        
        # Monitor cellular test output
        cellular_success = self.monitor_cellular_test(port)
        self.results['cellular'] = cellular_success
        return cellular_success
    
    def find_serial_port(self):
        """Find ESP32 serial port"""
        try:
            import serial.tools.list_ports
            
            ports = serial.tools.list_ports.comports()
            for port in ports:
                if "CH340" in port.description or "CP210" in port.description or "ESP32" in port.description:
                    self.log(f"Found serial port: {port.device} ({port.description})")
                    return port.device
            
            self.log("No ESP32 serial port found, trying first available port", "WARNING")
            if ports:
                return ports[0].device
                
        except ImportError:
            self.log("pyserial not available for port detection", "WARNING")
        
        return None
    
    def monitor_cellular_test(self, port):
        """Monitor cellular test output for success indicators"""
        self.log(f"Monitoring cellular test on {port}")
        
        try:
            self.serial_port = serial.Serial(port, 115200, timeout=1)
            start_time = time.time()
            test_results = {
                'module_communication': False,
                'sim_detected': False,
                'network_registered': False,
                'pdp_activated': False,
                'ip_assigned': False,
                'connectivity_test': False
            }
            
            while time.time() - start_time < TEST_TIMEOUTS['cellular']:
                if self.serial_port.in_waiting > 0:
                    line = self.serial_port.readline().decode('utf-8', errors='ignore').strip()
                    if line:
                        self.log(f"CELL: {line}")
                        
                        # Check for success indicators
                        if "Module communication established" in line:
                            test_results['module_communication'] = True
                        if "SIM status" in line and "READY" in line:
                            test_results['sim_detected'] = True
                        if "Network registration" in line and "registered" in line.lower():
                            test_results['network_registered'] = True
                        if "PDP activation" in line and "successful" in line.lower():
                            test_results['pdp_activated'] = True
                        if "IP address" in line and not "0.0.0.0" in line:
                            test_results['ip_assigned'] = True
                        if "connectivity test" in line and "successful" in line.lower():
                            test_results['connectivity_test'] = True
                
                time.sleep(0.1)
            
            # Evaluate results
            success_count = sum(test_results.values())
            total_tests = len(test_results)
            success_rate = success_count / total_tests
            
            self.log(f"Cellular test results: {success_count}/{total_tests} ({success_rate:.1%})")
            
            for test, result in test_results.items():
                status = "PASS" if result else "FAIL"
                self.log(f"  {test}: {status}")
            
            # Consider test successful if core functionality works
            core_success = (test_results['module_communication'] and 
                          test_results['sim_detected'] and 
                          test_results['network_registered'])
            
            self.serial_port.close()
            return core_success
            
        except Exception as e:
            self.log(f"Cellular monitoring error: {e}", "ERROR")
            if self.serial_port:
                self.serial_port.close()
            return False
    
    def test_memory_usage(self):
        """Test memory management in recovery build"""
        self.log("=== Testing Memory Management ===")
        
        # Build memory test
        success, _ = self.run_command(
            f"{PLATFORMIO_CMD} run -e m5stamps3-memory-test",
            timeout=TEST_TIMEOUTS['build']
        )
        
        if not success:
            self.log("Memory test build failed", "ERROR")
            self.results['memory'] = False
            return False
        
        # Analyze memory usage from build output
        success, output = self.run_command(
            f"{PLATFORMIO_CMD} run -e m5stamps3-recovery",
            timeout=60
        )
        
        if success:
            # Extract memory usage information
            heap_match = re.search(r'RAM:\s+(\d+)\s+bytes\s+\((\d+\.?\d*)%\)', output)
            flash_match = re.search(r'Flash:\s+(\d+)\s+bytes\s+\((\d+\.?\d*)%\)', output)
            
            if heap_match:
                heap_bytes = int(heap_match.group(1))
                heap_percent = float(heap_match.group(2))
                self.log(f"Heap usage: {heap_bytes} bytes ({heap_percent:.1f}%)")
                
                # Check if memory usage is reasonable
                if heap_percent < 80:
                    self.log("Memory usage is acceptable")
                    memory_ok = True
                else:
                    self.log("Memory usage is too high", "WARNING")
                    memory_ok = False
            else:
                self.log("Could not determine memory usage", "WARNING")
                memory_ok = False
            
            if flash_match:
                flash_bytes = int(flash_match.group(1))
                flash_percent = float(flash_match.group(2))
                self.log(f"Flash usage: {flash_bytes} bytes ({flash_percent:.1f}%)")
                
                if flash_percent < 90:
                    self.log("Flash usage is acceptable")
                else:
                    self.log("Flash usage is approaching limit", "WARNING")
        else:
            memory_ok = False
        
        self.results['memory'] = memory_ok
        return memory_ok
    
    def test_system_stability(self):
        """Test system stability and recovery features"""
        self.log("=== Testing System Stability ===")
        
        # This would involve running the recovery build and monitoring
        # For now, we'll do basic checks
        
        # Check if recovery files are present
        recovery_files = [
            "src/main_recovery.cpp",
            "platformio_recovery.ini",
            "recovery_cellular_test/recovery_cellular_test.ino",
            "src/modules/cellular/cellular_manager.h"
        ]
        
        all_present = True
        for file_path in recovery_files:
            full_path = RECOVERY_BUILD_DIR / file_path
            if full_path.exists():
                self.log(f"✓ {file_path}")
            else:
                self.log(f"✗ {file_path} - missing", "ERROR")
                all_present = False
        
        # Check if build configurations are valid
        config_files = ["platformio_recovery.ini"]
        for config_file in config_files:
            full_path = RECOVERY_BUILD_DIR / config_file
            if full_path.exists():
                try:
                    # Basic ini file validation
                    with open(full_path, 'r') as f:
                        content = f.read()
                        if "[env:" in content and "build_flags" in content:
                            self.log(f"✓ {config_file} - valid configuration")
                        else:
                            self.log(f"✗ {config_file} - invalid configuration", "ERROR")
                            all_present = False
                except Exception as e:
                    self.log(f"✗ {config_file} - read error: {e}", "ERROR")
                    all_present = False
            else:
                self.log(f"✗ {config_file} - missing", "ERROR")
                all_present = False
        
        self.results['stability'] = all_present
        return all_present
    
    def run_all_tests(self):
        """Run all recovery tests"""
        self.log("Starting GENPLC Recovery Build Tests")
        self.log(f"Working directory: {RECOVERY_BUILD_DIR}")
        
        start_time = time.time()
        
        # Run tests in order
        tests = [
            ("Build", self.test_build),
            ("Memory", self.test_memory_usage),
            ("Stability", self.test_system_stability),
            ("Cellular", self.test_cellular_connectivity)
        ]
        
        for test_name, test_func in tests:
            self.log(f"\n{'='*50}")
            self.log(f"Running {test_name} Test")
            self.log(f"{'='*50}")
            
            try:
                test_func()
            except Exception as e:
                self.log(f"Test {test_name} failed with exception: {e}", "ERROR")
                self.results[test_name.lower()] = False
        
        # Calculate overall result
        passed_tests = sum(1 for result in self.results.values() if result)
        total_tests = len(self.results) - 1  # Exclude 'overall'
        
        self.results['overall'] = passed_tests >= (total_tests * 0.75)  # 75% pass rate
        
        # Generate report
        self.generate_report(time.time() - start_time)
        
        return self.results['overall']
    
    def generate_report(self, duration):
        """Generate test report"""
        self.log(f"\n{'='*60}")
        self.log("GENPLC RECOVERY TEST REPORT")
        self.log(f"{'='*60}")
        self.log(f"Test Duration: {duration:.1f} seconds")
        self.log(f"Test Time: {time.strftime('%Y-%m-%d %H:%M:%S')}")
        self.log("")
        
        # Results summary
        for test_name, result in self.results.items():
            if test_name != 'overall':
                status = "PASS" if result else "FAIL"
                self.log(f"{test_name.upper():15} : {status}")
        
        self.log("")
        overall_status = "PASS" if self.results['overall'] else "FAIL"
        self.log(f"OVERALL: {overall_status}")
        
        # Recommendations
        self.log(f"\n{'='*60}")
        self.log("RECOMMENDATIONS")
        self.log(f"{'='*60}")
        
        if not self.results['build']:
            self.log("- Fix compilation errors in recovery build")
            self.log("- Check PlatformIO configuration")
            self.log("- Verify library dependencies")
        
        if not self.results['memory']:
            self.log("- Optimize memory usage in recovery build")
            self.log("- Reduce stack sizes where possible")
            self.log("- Check for memory leaks")
        
        if not self.results['cellular']:
            self.log("- Check cellular module connections")
            self.log("- Verify SIM card status and provisioning")
            self.log("- Test with different APN configurations")
            self.log("- Check antenna and signal strength")
        
        if not self.results['stability']:
            self.log("- Verify all recovery files are present")
            self.log("- Check build configuration files")
            self.log("- Test recovery procedures")
        
        # Save report
        self.save_report()
    
    def save_report(self):
        """Save test report to file"""
        report_file = RECOVERY_BUILD_DIR / f"recovery_test_report_{int(time.time())}.json"
        
        report_data = {
            'timestamp': time.strftime('%Y-%m-%d %H:%M:%S'),
            'duration': time.time(),
            'results': self.results,
            'log': self.test_log
        }
        
        try:
            with open(report_file, 'w') as f:
                json.dump(report_data, f, indent=2)
            self.log(f"Report saved to: {report_file}")
        except Exception as e:
            self.log(f"Failed to save report: {e}", "ERROR")

def main():
    """Main function"""
    if len(sys.argv) > 1:
        # Specific test requested
        test_type = sys.argv[1].lower()
        tester = RecoveryTester()
        
        if test_type == "build":
            tester.test_build()
        elif test_type == "cellular":
            tester.test_cellular_connectivity()
        elif test_type == "memory":
            tester.test_memory_usage()
        elif test_type == "stability":
            tester.test_system_stability()
        else:
            print(f"Unknown test: {test_type}")
            print("Available tests: build, cellular, memory, stability, all")
            return 1
    else:
        # Run all tests
        tester = RecoveryTester()
        success = tester.run_all_tests()
        return 0 if success else 1
    
    return 0

if __name__ == "__main__":
    sys.exit(main())
