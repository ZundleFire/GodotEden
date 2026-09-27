"""
GodotEden E2E Test Suite Framework.

Provides base test suite harness, assertion framework, test registration,
setup/teardown lifecycle, and tier tagging for E2E verification.
"""

import enum
import sys
import time
import traceback
from dataclasses import dataclass, field
from typing import Any, Callable, Dict, List, Optional, Type, Union


class TestStatus(enum.Enum):
    PASS = "PASS"
    FAIL = "FAIL"
    SKIP = "SKIP"
    ERROR = "ERROR"


@dataclass
class TestResult:
    test_id: str
    feature_id: str
    tier: int
    description: str
    status: TestStatus
    duration_seconds: float
    error_message: Optional[str] = None
    traceback: Optional[str] = None

    def to_dict(self) -> Dict[str, Any]:
        return {
            "test_id": self.test_id,
            "feature_id": self.feature_id,
            "tier": self.tier,
            "description": self.description,
            "status": self.status.value,
            "duration_seconds": round(self.duration_seconds, 6),
            "error_message": self.error_message,
            "traceback": self.traceback,
        }


class AssertionError(Exception):
    """Custom assertion error for E2E framework assertions."""
    pass


class E2ETestContext:
    """Execution context passed into E2E test functions.
    
    Provides logging, assertion helpers, state tracking, and resource cleanup.
    """

    def __init__(self, test_id: str):
        self.test_id = test_id
        self.logs: List[str] = []
        self._cleanups: List[Callable[[], None]] = []

    def log(self, message: str) -> None:
        formatted = f"[{self.test_id}] {message}"
        self.logs.append(formatted)

    def add_cleanup(self, cleanup_func: Callable[[], None]) -> None:
        self._cleanups.append(cleanup_func)

    def cleanup(self) -> None:
        for cleanup in reversed(self._cleanups):
            try:
                cleanup()
            except Exception as e:
                self.log(f"Cleanup error: {e}")
        self._cleanups.clear()

    # --- Assertion Library ---

    def assert_equal(self, actual: Any, expected: Any, msg: Optional[str] = None) -> None:
        if actual != expected:
            error_msg = msg or f"Expected {expected!r}, got {actual!r}"
            raise AssertionError(error_msg)

    def assert_not_equal(self, actual: Any, expected: Any, msg: Optional[str] = None) -> None:
        if actual == expected:
            error_msg = msg or f"Expected value not equal to {expected!r}"
            raise AssertionError(error_msg)

    def assert_true(self, condition: Any, msg: Optional[str] = None) -> None:
        if not condition:
            error_msg = msg or f"Expected True condition, got {condition!r}"
            raise AssertionError(error_msg)

    def assert_false(self, condition: Any, msg: Optional[str] = None) -> None:
        if condition:
            error_msg = msg or f"Expected False condition, got {condition!r}"
            raise AssertionError(error_msg)

    def assert_none(self, val: Any, msg: Optional[str] = None) -> None:
        if val is not None:
            error_msg = msg or f"Expected None, got {val!r}"
            raise AssertionError(error_msg)

    def assert_not_none(self, val: Any, msg: Optional[str] = None) -> None:
        if val is None:
            error_msg = msg or "Expected non-None value, got None"
            raise AssertionError(error_msg)

    def assert_almost_equal(self, actual: float, expected: float, delta: float = 1e-5, msg: Optional[str] = None) -> None:
        if abs(actual - expected) > delta:
            error_msg = msg or f"Expected {expected} +/- {delta}, got {actual}"
            raise AssertionError(error_msg)

    def assert_in(self, member: Any, container: Any, msg: Optional[str] = None) -> None:
        if member not in container:
            error_msg = msg or f"Expected {member!r} to be in {container!r}"
            raise AssertionError(error_msg)

    def assert_greater(self, a: Any, b: Any, msg: Optional[str] = None) -> None:
        if not (a > b):
            error_msg = msg or f"Expected {a!r} > {b!r}"
            raise AssertionError(error_msg)

    def assert_less(self, a: Any, b: Any, msg: Optional[str] = None) -> None:
        if not (a < b):
            error_msg = msg or f"Expected {a!r} < {b!r}"
            raise AssertionError(error_msg)

    def assert_raises(self, expected_exception: Type[BaseException], func: Callable, *args: Any, **kwargs: Any) -> None:
        try:
            func(*args, **kwargs)
        except expected_exception:
            return
        except Exception as e:
            raise AssertionError(f"Expected exception {expected_exception.__name__}, but caught {type(e).__name__}: {e}")
        raise AssertionError(f"Expected exception {expected_exception.__name__}, but no exception was raised.")


class E2ETestCase:
    """Represents an individual E2E test case specification and execution logic."""

    def __init__(
        self,
        test_id: str,
        feature_id: str,
        tier: int,
        description: str,
        func: Callable[[E2ETestContext], None],
        setup_func: Optional[Callable[[E2ETestContext], None]] = None,
        teardown_func: Optional[Callable[[E2ETestContext], None]] = None,
    ):
        self.test_id = test_id
        self.feature_id = feature_id
        self.tier = tier
        self.description = description
        self.func = func
        self.setup_func = setup_func
        self.teardown_func = teardown_func

    def run(self) -> TestResult:
        context = E2ETestContext(self.test_id)
        start_time = time.perf_counter()
        status = TestStatus.PASS
        error_msg: Optional[str] = None
        tb_str: Optional[str] = None

        try:
            if self.setup_func:
                self.setup_func(context)
            self.func(context)
        except AssertionError as ae:
            status = TestStatus.FAIL
            error_msg = str(ae)
            tb_str = traceback.format_exc()
        except Exception as e:
            status = TestStatus.ERROR
            error_msg = f"{type(e).__name__}: {e}"
            tb_str = traceback.format_exc()
        finally:
            if self.teardown_func:
                try:
                    self.teardown_func(context)
                except Exception as te:
                    context.log(f"Teardown error: {te}")
            context.cleanup()

        duration = time.perf_counter() - start_time
        return TestResult(
            test_id=self.test_id,
            feature_id=self.feature_id,
            tier=self.tier,
            description=self.description,
            status=status,
            duration_seconds=duration,
            error_message=error_msg,
            traceback=tb_str,
        )


class TestRegistry:
    """Global or isolated registry for storing and querying E2E test cases."""

    def __init__(self):
        self._tests: Dict[str, E2ETestCase] = {}

    def register(self, test_case: E2ETestCase) -> None:
        if test_case.test_id in self._tests:
            raise ValueError(f"Duplicate test ID registered: {test_case.test_id}")
        self._tests[test_case.test_id] = test_case

    def get_all_tests(self) -> List[E2ETestCase]:
        return list(self._tests.values())

    def filter_tests(
        self, tier: Optional[int] = None, feature: Optional[str] = None
    ) -> List[E2ETestCase]:
        results = []
        for test in self._tests.values():
            if tier is not None and test.tier != tier:
                continue
            if feature is not None:
                feature_upper = feature.upper()
                if feature_upper not in test.feature_id.upper() and feature_upper not in test.description.upper():
                    continue
            results.append(test)
        return results

    def clear(self) -> None:
        self._tests.clear()


_GLOBAL_REGISTRY = TestRegistry()


def get_global_registry() -> TestRegistry:
    return _GLOBAL_REGISTRY


def e2e_test(
    test_id: str,
    feature_id: str,
    tier: int,
    description: str,
    setup_func: Optional[Callable[[E2ETestContext], None]] = None,
    teardown_func: Optional[Callable[[E2ETestContext], None]] = None,
    registry: Optional[TestRegistry] = None,
):
    """Decorator to register a function as an E2E test case."""
    target_registry = registry if registry is not None else _GLOBAL_REGISTRY

    def decorator(func: Callable[[E2ETestContext], None]):
        test_case = E2ETestCase(
            test_id=test_id,
            feature_id=feature_id,
            tier=tier,
            description=description,
            func=func,
            setup_func=setup_func,
            teardown_func=teardown_func,
        )
        target_registry.register(test_case)
        return func

    return decorator
