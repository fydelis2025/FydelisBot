"""Protocolo FydelisBot v1 — texto, uma mensagem por linha, terminador \\n.

Cliente -> servidor
    HELLO <versao>              handshake
    SET <seq> <junta> <rad>     comando de posição (coalescido pelo cliente)
    ESTOP                       paragem de emergência (trava até ENABLE)
    ENABLE                      rearma
    PING                        keepalive

Servidor -> cliente
    WELCOME fydelis/<versao>
    OK <seq> <junta> <rad_aplicado>
    OK ESTOP | OK ENABLE | PONG
    ERR <motivo>

Compatibilidade: aceita o formato legado "<junta>:<rad>" (desencorajado).
"""

from __future__ import annotations

import math
import re
from dataclasses import dataclass


MAX_LINE = 512
PROTOCOL_VERSION = "1"
_JOINT_RE = re.compile(r"^[a-z][a-z0-9_]{0,63}$")


class ProtocolError(ValueError):
    """Linha malformada ou comando inválido."""


class CommandRejected(RuntimeError):
    """Comando válido mas recusado pelo estado atual (ex.: E-STOP ativo)."""


@dataclass(frozen=True)
class Hello:
    version: str


@dataclass(frozen=True)
class SetPosition:
    seq: int
    joint: str
    value: float
    clamped: bool = False


@dataclass(frozen=True)
class Control:
    """ESTOP | ENABLE | PING"""
    name: str


def _parse_float(token: str) -> float:
    try:
        value = float(token)
    except ValueError as exc:
        raise ProtocolError(f"valor numérico inválido: {token!r}") from exc
    if not math.isfinite(value):
        raise ProtocolError("valor não finito")
    return value


def parse_line(
    line: str,
    joints: frozenset[str],
    limits: dict[str, tuple[float, float]],
) -> Hello | SetPosition | Control:
    text = line.strip()
    if not text:
        raise ProtocolError("linha vazia")

    # --- legado "junta:valor" ---
    if ":" in text and " " not in text:
        joint, _, raw = text.partition(":")
        return _make_set(0, joint, raw, joints, limits)

    parts = text.split()
    verb = parts[0].upper()

    if verb == "HELLO":
        if len(parts) != 2:
            raise ProtocolError("HELLO requer exatamente 1 argumento")
        return Hello(version=parts[1])

    if verb in ("ESTOP", "ENABLE", "PING"):
        if len(parts) != 1:
            raise ProtocolError(f"{verb} não aceita argumentos")
        return Control(name=verb)

    if verb == "SET":
        if len(parts) != 4:
            raise ProtocolError("SET requer <seq> <junta> <radianos>")
        try:
            seq = int(parts[1], 10)
        except ValueError as exc:
            raise ProtocolError(f"seq inválido: {parts[1]!r}") from exc
        if seq < 0:
            raise ProtocolError("seq negativo")
        return _make_set(seq, parts[2], parts[3], joints, limits)

    raise ProtocolError(f"comando desconhecido: {verb!r}")


def _make_set(
    seq: int, joint: str, raw: str,
    joints: frozenset[str],
    limits: dict[str, tuple[float, float]],
) -> SetPosition:
    joint = joint.strip()
    if not _JOINT_RE.match(joint) or joint not in joints:
        raise ProtocolError(f"junta desconhecida: {joint!r}")

    value = _parse_float(raw)
    lo, hi = limits[joint]
    clamped = not (lo <= value <= hi)
    return SetPosition(seq=seq, joint=joint, value=min(max(value, lo), hi), clamped=clamped)


def format_ack(cmd: SetPosition, applied: float) -> str:
    if cmd.seq <= 0:          # legado: sem seq para ecoar
        return f"OK {cmd.joint} {applied:.4f}"
    return f"OK {cmd.seq} {cmd.joint} {applied:.4f}"
