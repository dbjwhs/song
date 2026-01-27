# MIT License
# Copyright (c) 2026 dbjwhs

"""
Test generated Python client against C++ TCP Calculator service.

This test verifies that the songc --lang=python generated code works correctly.
"""

import unittest
import subprocess
import time
from pathlib import Path

from song.connection import ServiceConnection
from song.generated.calculator import CalculatorProxy, DivResult


TEST_PORT = 18889  # Different port to avoid conflicts


def find_service_executable() -> Path:
    """Find the TCP calculator service executable."""
    candidates = [
        Path(__file__).parent.parent.parent / "build" / "sing" / "network" / "tcp_calculator" / "sing_network_tcp_calculator_service",
    ]

    for path in candidates:
        if path.exists():
            return path

    raise FileNotFoundError("TCP calculator service not found. Run 'make' in build directory first.")


class TestGeneratedCalculator(unittest.TestCase):
    """Tests for generated CalculatorProxy."""

    server_process = None

    @classmethod
    def setUpClass(cls):
        """Start the TCP calculator service before tests."""
        try:
            service_path = find_service_executable()
        except FileNotFoundError as e:
            raise unittest.SkipTest(str(e))

        # Start the service
        cls.server_process = subprocess.Popen(
            [str(service_path), str(TEST_PORT)],
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )

        # Wait for service to start
        for _ in range(20):
            time.sleep(0.1)
            try:
                import socket
                sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
                sock.settimeout(0.5)
                sock.connect(("127.0.0.1", TEST_PORT))
                sock.close()
                return
            except (socket.error, ConnectionRefusedError):
                pass

        cls.server_process.terminate()
        cls.server_process = None
        raise unittest.SkipTest("Failed to start TCP calculator service")

    @classmethod
    def tearDownClass(cls):
        """Stop the TCP calculator service after tests."""
        if cls.server_process:
            cls.server_process.terminate()
            cls.server_process.wait()

    def test_add(self):
        """Test generated add method."""
        with ServiceConnection("127.0.0.1", TEST_PORT) as conn:
            calc = CalculatorProxy(conn)
            self.assertEqual(calc.add(2, 3), 5)
            self.assertEqual(calc.add(-10, 20), 10)

    def test_subtract(self):
        """Test generated subtract method."""
        with ServiceConnection("127.0.0.1", TEST_PORT) as conn:
            calc = CalculatorProxy(conn)
            self.assertEqual(calc.subtract(10, 4), 6)
            self.assertEqual(calc.subtract(5, 10), -5)

    def test_multiply(self):
        """Test generated multiply method."""
        with ServiceConnection("127.0.0.1", TEST_PORT) as conn:
            calc = CalculatorProxy(conn)
            self.assertEqual(calc.multiply(6, 7), 42)

    def test_divide(self):
        """Test generated divide method with DivResult struct."""
        with ServiceConnection("127.0.0.1", TEST_PORT) as conn:
            calc = CalculatorProxy(conn)
            result = calc.divide(17, 5)
            self.assertIsInstance(result, DivResult)
            self.assertEqual(result.quotient, 3)
            self.assertEqual(result.remainder, 2)

    def test_factorial(self):
        """Test generated factorial method."""
        with ServiceConnection("127.0.0.1", TEST_PORT) as conn:
            calc = CalculatorProxy(conn)
            self.assertEqual(calc.factorial(0), 1)
            self.assertEqual(calc.factorial(5), 120)
            self.assertEqual(calc.factorial(10), 3628800)

    def test_sum(self):
        """Test generated sum method with array parameter."""
        with ServiceConnection("127.0.0.1", TEST_PORT) as conn:
            calc = CalculatorProxy(conn)
            self.assertEqual(calc.sum([1, 2, 3, 4, 5]), 15)
            self.assertEqual(calc.sum([]), 0)
            self.assertEqual(calc.sum(list(range(1, 101))), 5050)


if __name__ == "__main__":
    unittest.main()
