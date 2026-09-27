"""
GodotEden End-to-End (E2E) Test Suite Package.
"""

from tests.e2e.framework import (
    e2e_test,
    E2ETestCase,
    E2ETestContext,
    TestRegistry,
    TestResult,
    TestStatus,
    get_global_registry,
)

__all__ = [
    "e2e_test",
    "E2ETestCase",
    "E2ETestContext",
    "TestRegistry",
    "TestResult",
    "TestStatus",
    "get_global_registry",
]
