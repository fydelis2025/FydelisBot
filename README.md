fydelisbot/
├── gui/          transport.[ch]  main.c  meson.build  Makefile
├── bridge/       fydelis_bridge/{protocol,controller,server,node}.py
│                 test/test_protocol.py  config/bridge.yaml
└── description/  urdf/fydelisbot.urdf.xacro  launch/  rviz/

---

## 2. Matriz de testes

O que já está coberto por `test_protocol.py` (unitário, sem ROS, sem rede):

| ID | Cenário | Esperado | Coberto |
|---|---|---|---|
| P1 | `SET` válido | `SetPosition(seq, joint, value, clamped=False)` | ✅ |
| P2 | valor acima do limite | `clamped=True`, valor limitado | ✅ |
| P3 | junta desconhecida | `ProtocolError` | ✅ |
| P4 | `nan` / `inf` | `ProtocolError` | ✅ |
| P5 | vírgula decimal `1,57` | `ProtocolError` | ✅ |
| P6 | `ESTOP` / `ping` minúsculas | `Control("ESTOP")` / `Control("PING")` | ✅ |
| P7 | formato legado | `seq == 0` | ✅ |
| P8 | linha vazia / `SET` com 3 campos | `ProtocolError` | ✅ |
| P9 | junta com maiúsculas ou `../` | `ProtocolError` (regex `_JOINT_RE`) | ✅ |
| P10 | `seq` negativo ou não inteiro | `ProtocolError` | ✅ |

O que **falta** implementar (deixo explícito, para não parecer coberto):

| ID | Cenário | Onde | Esperado |
|---|---|---|---|
| C1 | `step()` com `dt` grande não salta | `test_controller.py` | `|Δ| ≤ max_vel*dt` |
| C2 | `set_target` com E-STOP ativo | `test_controller.py` | `CommandRejected` |
| C3 | watchdog congela após `command_timeout` | `test_controller.py` | posições estáveis |
| C4 | `dt=0` não divide por zero | `test_controller.py` | velocidade 0.0 |
| C5 | duas linhas num só `recv` | integração | ambas processadas |
| C6 | linha > 512 bytes | integração | `ERR` + fecho |
| C7 | `max_clients` excedido | integração | `ERR servidor ocupado` |
| C8 | rate limit | integração | `ERR rate limit` + fecho |
| C9 | cliente inativo > `idle_timeout` | integração | fecho limpo, sem exceção |
| C10 | bridge morto a meio da sessão | caos | GUI mostra "Sem ligação" e reconecta |
| C11 | GUI morta com E-STOP inativo | caos | watchdog congela juntas em ≤ 1 s |
| C12 | `LC_NUMERIC=pt_PT.UTF-8` | caos | protocolo continua válido |

C5–C9 usam `socketserver` real numa porta efémera (`port=0`) — não precisam de ROS.

---

## 3. Checklist de robustez

**Transporte / rede**
- [x] Ligação persistente, thread dedicada, main loop do GTK nunca bloqueia
- [x] `connect()` não bloqueante com `poll()` e timeout
- [x] `TCP_NODELAY` + `SO_KEEPALIVE`
- [x] `send_all()` trata escritas parciais, `EINTR`, `EAGAIN`
- [x] `MSG_NOSIGNAL` (sem `SIGPIPE`)
- [x] Reconexão com backoff exponencial 100 ms → 2 s
- [x] Coalescência: N eventos de slider → 1 comando por junta
- [x] Comandos acumulados durante a queda são descartados; re-sync ao reconectar
- [x] Refcount nas notificações de estado (sem UAF)
- [ ] TLS ou túnel obrigatório fora de loopback

**Protocolo**
- [x] *Framing* por linha com `readline(MAX_LINE+2)` — nunca parse de buffer cru
- [x] Máximo de 512 bytes por linha
- [x] Encoding ASCII explícito, `UnicodeDecodeError` não mata a thread
- [x] Versão no handshake
- [x] `seq` para correlação pedido/resposta
- [ ] `seq` monotónico por sessão (anti-replay)

**Controlo**
- [x] Limites de junta no servidor (`clamp` + `ERR`)
- [x] Valores finitos (`nan`/`inf` rejeitados)
- [x] Watchdog de comandos
- [x] Limite de velocidade (slew rate)
- [x] E-STOP que trava de facto, não só a UI
- [x] `dt` clampado a 0.25 s contra stalls
- [x] Publicação com `velocity` preenchida (RViz e `robot_state_publisher` agradecem)
- [ ] `effort` real (placeholder a zeros)

**ROS 2**
- [x] Parâmetros declarados com defaults seguros
- [x] `RuntimeError` claro em `EADDRINUSE`
- [x] `destroy_node()` fecha o servidor
- [x] `rclpy.shutdown()` só se `rclpy.ok()` (evita exceção no SIGINT)
- [x] QoS `RELIABLE` explícito
- [ ] `ros2_control` para hardware (`<ros2_control>` comentado, por ativar)
- [ ] Testes `launch_testing` do launch file

**Código / build**
- [x] Sem duplicação de limites *dentro* de cada camada; GUI/URDF/yaml espelham-se
- [x] `g_ascii_formatd` em vez de `%f`
- [x] `-Wall -Wextra -Wpedantic -Wshadow -Wconversion`
- [x] Pacotes separados, sem dependências circulares
- [x] Lógica pura (protocolo, controlador) isolada de ROS e de sockets → testável
- [ ] CI (GitHub Actions: `colcon test` + `meson test`)

---

## 4. Limitações conhecidas — leitura honesta do estado atual

Não compilei nem executei nada nesta conversa; o que afirmo é o resultado de revisão, não de teste. Antes de confiar no código:

1. **`colcon build` vai falhar na primeira tentativa ao xacro** se aplicares só parte da correção 0.1 — o erro `Unused parameter: side` é fatal.
2. **Não criei `resource/fydelis_bridge`** (ficheiro vazio) nem `setup.cfg` completo. Sem eles o `colcon` não instala o entry point. Ficheiros necessários:

3. **`%conversion` no C**: o `transport.c` faz `(gsize) n` e `g_ascii_strtoull` — com `-Wconversion` vão aparecer *warnings* de narrowing (`guint16` de `strtoull`, `gsize` de `ssize_t`). Compila, mas o build não fica limpo. Casts explícitos resolvem.
4. **`interfaces` do protocolo**: `Controller.step()` devolve `(estop, stale)`; `stale` só muda de valor em transição, mas o log compara com `_reported_stale` — correto. Contudo, após um E-STOP seguido de `ENABLE` sem comandos novos, `_last_cmd` é atualizado e o watchdog reinicia. É o comportamento desejado, mas convém documentá-lo.
5. **Duplicação de limites em três lugares** (tabela `k_joints` em C, `bridge.yaml`, URDF). É dívida técnica deliberada: eliminar exige geração de código ou leitura de parâmetros ROS pela GUI. Se quiseres resolver, o caminho é um `joints.json`/`joints.yaml` único que gera os dois.
6. **Sem autenticação.** Repito porque é o item mais importante: em `listen_host: 0.0.0.0` qualquer host da rede move o robô. O `WARN` no log não é controlo.

---

## 5. Se quiseres endurecer o canal (próximo passo natural)

A evolução com melhor relação esforço/benefício é **Unix domain socket com `SO_PEERCRED`**, porque dá identidade real do processo do mesmo host sem criptografia:

```python
# server.py — variante UDS
def _peer_uid(self) -> int:
 creds = self.connection.getsockopt(
     socket.SOL_SOCKET, socket.SO_PEERCRED, struct.calcsize("3i")
 )
 _pid, uid, _gid = struct.unpack("3i", creds)
 return uid

# em setup() ou no início de handle():
allowed = self.server.allowed_uids
if allowed and self._peer_uid() not in allowed:
 self._send("ERR uid não autorizado")
 return
