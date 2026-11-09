# 🤖 FydelisBot

<div align="center">

[![ROS 2](https://img.shields.io/badge/ROS%202-Humble%2FIron-22314E?style=for-the-badge&logo=ros&logoColor=white)]()
[![C](https://img.shields.io/badge/Language-C-00599C?style=for-the-badge&logo=c&logoColor=white)]()
[![X11](https://img.shields.io/badge/GUI-X11%20Libraries-orange?style=for-the-badge)]()
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg?style=for-the-badge)](LICENSE)

**Um projeto avançado de robótica humanóide com modelagem estrutural em ROS 2 (URDF/Xacro) e um painel de controle gráfico de alta performance desenvolvido nativamente em C.**

</div>

---

## 🧭 Sobre o Projeto

O **FydelisBot** foi desenvolvido para integrar simulação de robótica avançada com controle de baixo nível. O projeto divide-se em duas frentes principais:
1. **Modelo Cinemático e Dinâmico:** Especificado via ROS 2 utilizando URDF/Xacro para simulação realista de juntas, esqueleto e sensores.
2. **Painel de Controle Gráfico (`fyd_painel.c`):** Uma interface gráfica robusta escrita puramente em C utilizando as bibliotecas **X11**, permitindo monitoramento de telemetria, comandos de movimentação e status dos atuadores em tempo real sem o overhead de frameworks pesados.

---

## ⚙️ Arquitetura do Sistema

---

## 🛠️ Tecnologias Utilizadas

* **Robótica:** ROS 2 (Robot Operating System), URDF, Xacro, RViz2.
* **Linguagens:** C (para o painel de controle e lógica de baixo nível), XML/Python (para pacotes ROS).
* **Interface Gráfica (GUI):** X11 Core Protocol (`libX11`) para renderização direta e responsiva da telemetria.

---

## 🚀 Como Executar

### Pré-requisitos
* **ROS 2** (Humble Hawksbill ou superior recomendado) instalado.
* Bibliotecas de desenvolvimento X11 (`libx11-dev`).

No Debian/Ubuntu, instale as dependências do X11 com:
```bash
sudo apt update && sudo apt install libx11-dev build-essential

git clone [https://github.com/fydelis2025/FydelisBot.git](https://github.com/fydelis2025/FydelisBot.git)
cd FydelisBot
