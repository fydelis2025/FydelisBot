from __future__ import annotations

import logging
import socket
import socketserver
import threading
import time

from .protocol import (
    PROTOCOL_VERSION,
    MAX_LINE,
    CommandRejected,
    Control,
    Hello,
    ProtocolError,
    SetPosition,
    format_ack,
    parse_line,
)

log = logging.getLogger(__name__)


class _Handler(socketserver.StreamRequestHandler):
    def setup(self) -> None:
        super().setup()
        self.connection.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
        self.connection.settimeout(self.server.idle_timeout)
        self._window_start = time.monotonic()
        self._window_count = 0

    def _send(self, text: str) -> bool:
        try:
            self.wfile.write(text.encode("ascii") + b"\n")
            self.wfile.flush()
            return True
        except OSError:
            return False

    def handle(self) -> None:
        srv = self.server
        ctrl = srv.controller
        peer = f"{self.client_address[0]}:{self.client_address[1]}"
        log.info("cliente ligado: %s", peer)

        if not self._send(f"WELCOME fydelis/{PROTOCOL_VERSION}"):
            return

        try:
            while srv.running:
                try:
                    raw = self.rfile.readline(MAX_LINE + 2)
                except (TimeoutError, socket.timeout):
                    log.info("cliente %s inativo — a fechar", peer)
                    return
                except OSError:
                    return

                if not raw:
                    return
                if len(raw) > MAX_LINE or not raw.endswith(b"\n"):
                    self._send("ERR linha demasiado longa")
                    return

                # rate limit simples (janela de 1 s)
                now = time.monotonic()
                if now - self._window_start >= 1.0:
                    self._window_start, self._window_count = now, 0
                self._window_count += 1
                if self._window_count > srv.max_command_rate:
                    self._send("ERR rate limit")
                    return

                try:
                    line = raw.decode("ascii").rstrip("\r\n")
                    cmd = parse_line(line, ctrl.joint_set, ctrl.limits)
                except UnicodeDecodeError:
                    self._send("ERR encoding (esperado ASCII)")
                    continue
                except ProtocolError as exc:
                    self._send(f"ERR {exc}")
                    continue

                if isinstance(cmd, Hello):
                    log.info("handshake %s de %s", cmd.version, peer)
                elif isinstance(cmd, Control):
                    self._handle_control(cmd)
                elif isinstance(cmd, SetPosition):
                    try:
                        applied = ctrl.set_target(cmd.joint, cmd.value)
                    except CommandRejected as exc:
                        self._send(f"ERR {exc}")
                        continue
                    if cmd.clamped:
                        log.warning("valor fora de limites: %s pedido=%.3f aplicado=%.3f",
                                    cmd.joint, cmd.value, applied)
                    self._send(format_ack(cmd, applied))
        finally:
            log.info("cliente desligado: %s", peer)

    def _handle_control(self, cmd: Control) -> None:
        ctrl = self.server.controller
        if cmd.name == "ESTOP":
            ctrl.estop()
            log.warning("E-STOP ativado por %s", self.client_address[0])
            self._send("OK ESTOP")
        elif cmd.name == "ENABLE":
            ctrl.enable()
            log.warning("E-STOP removido por %s", self.client_address[0])
            self._send("OK ENABLE")
        else:
            self._send("PONG")


class CommandServer(socketserver.ThreadingTCPServer):
    allow_reuse_address = True
    daemon_threads = True
    request_queue_size = 8

    def __init__(self, address, controller, *, idle_timeout: float,
                 max_command_rate: float, max_clients: int) -> None:
        self.controller = controller
        self.idle_timeout = idle_timeout
        self.max_command_rate = max_command_rate
        self.running = True
        self._slots = threading.BoundedSemaphore(max_clients)
        super().__init__(address, _Handler)

    def verify_request(self, request, client_address) -> bool:
        """Recusa liminarmente se não houver vaga — sem threads órfãs."""
        if self._slots.acquire(blocking=False):
            return True
        log.warning("limite de clientes atingido — a recusar %s", client_address[0])
        try:
            request.sendall(b"ERR servidor ocupado\n")
        except OSError:
            pass
        return False

    def process_request_thread(self, request, client_address):
        try:
            super().process_request_thread(request, client_address)
        finally:
            self._slots.release()

    def shutdown_server(self) -> None:
        self.running = False
        self.shutdown()
        self.server_close()
