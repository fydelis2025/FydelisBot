from __future__ import annotations

import threading
import time

from .protocol import CommandRejected


class JointController:
    """Alvos + posições atuais, com limite de velocidade, watchdog e E-STOP."""

    def __init__(
        self,
        names: list[str],
        lower: list[float],
        upper: list[float],
        max_velocity: float,
        command_timeout: float,
    ) -> None:
        if not (len(names) == len(lower) == len(upper)):
            raise ValueError("names/lower/upper com dimensões diferentes")
        if any(lo >= hi for lo, hi in zip(lower, upper)):
            raise ValueError("limite inferior >= superior")

        self.names = tuple(names)
        self.limits = {n: (lo, hi) for n, lo, hi in zip(names, lower, upper)}
        self.joint_set = frozenset(names)

        self._max_vel = float(max_velocity)
        self._timeout = float(command_timeout)

        self._lock = threading.Lock()
        self._target = dict.fromkeys(self.names, 0.0)
        self._position = dict.fromkeys(self.names, 0.0)
        self._velocity = dict.fromkeys(self.names, 0.0)
        self._estop = False
        self._last_cmd = 0.0
        self._stale = False

    # ---------------- comandos (chamados pela thread do servidor) ----------------

    def set_target(self, joint: str, value: float) -> float:
        with self._lock:
            if self._estop:
                raise CommandRejected("E-STOP ativo — rearme primeiro (ENABLE)")
            self._target[joint] = value
            self._last_cmd = time.monotonic()
            self._stale = False
            return self._target[joint]

    def estop(self) -> None:
        with self._lock:
            self._estop = True
            self._target = dict(self._position)   # congela no sítio

    def enable(self) -> None:
        with self._lock:
            self._estop = False
            self._target = dict(self._position)
            self._last_cmd = time.monotonic()

    @property
    def estopped(self) -> bool:
        with self._lock:
            return self._estop

    # ---------------- ciclo de controlo ----------------

    def step(self, dt: float) -> tuple[bool, bool]:
        """Integra um passo. Devolve (estop, stale)."""
        with self._lock:
            stale = (time.monotonic() - self._last_cmd) > self._timeout
            if stale and not self._stale:
                self._stale = True
            step_max = self._max_vel * max(dt, 0.0)

            for name in self.names:
                cur = self._position[name]
                tgt = self._target[name]
                if self._estop or stale:
                    tgt = cur
                    self._target[name] = cur
                delta = max(-step_max, min(step_max, tgt - cur))
                self._position[name] = cur + delta
                self._velocity[name] = (delta / dt) if dt > 0.0 else 0.0

            return self._estop, stale

    def snapshot(self) -> tuple[list[float], list[float]]:
        with self._lock:
            return (
                [self._position[n] for n in self.names],
                [self._velocity[n] for n in self.names],
            )
