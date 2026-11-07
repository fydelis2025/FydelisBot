import pytest

from fydelis_bridge.protocol import (
    Control, Hello, ProtocolError, SetPosition, format_ack, parse_line,
)

JOINTS = frozenset({"neck_joint", "left_hip_joint"})
LIMITS = {"neck_joint": (-1.57, 1.57), "left_hip_joint": (-1.57, 1.57)}


def p(line):
    return parse_line(line, JOINTS, LIMITS)


def test_set_valido():
    c = p("SET 7 neck_joint 0.5")
    assert c == SetPosition(seq=7, joint="neck_joint", value=0.5, clamped=False)


def test_set_clampa():
    c = p("SET 1 left_hip_joint 99")
    assert c.value == pytest.approx(1.57) and c.clamped


def test_junta_desconhecida():
    with pytest.raises(ProtocolError):
        p("SET 1 evil_joint 0.0")


def test_valor_nao_finito():
    with pytest.raises(ProtocolError):
        p("SET 1 neck_joint nan")


def test_virgula_decimal_rejeitada():
    with pytest.raises(ProtocolError):
        p("SET 1 neck_joint 1,57")


def test_controle():
    assert p("ESTOP") == Control("ESTOP")
    assert p("ping") == Control("PING")


def test_legado():
    c = p("neck_joint:0.25")
    assert c.joint == "neck_joint" and c.seq == 0


def test_ack():
    assert format_ack(SetPosition(3, "neck_joint", 0.1), 0.1).startswith("OK 3 neck_joint")
