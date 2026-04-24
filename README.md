# BCC264 — TP2: Servidor TCP Concorrente com Protocolo Próprio

**Disciplina:** BCC264 - Sistemas Operacionais  
**Instituição:** DECOM/UFOP — Universidade Federal de Ouro Preto  
**Professor:** Prof. Dr. Carlos Frederico M. C. Cavalcanti  

---

## Sumário

1. [Visão geral](#1-visão-geral)
2. [Estrutura do repositório](#2-estrutura-do-repositório)
3. [Protocolo de aplicação](#3-protocolo-de-aplicação)
4. [Pré-requisitos](#4-pré-requisitos)
5. [Compilação local](#5-compilação-local)
6. [Execução local](#6-execução-local)
7. [Execução via Docker](#7-execução-via-docker)
8. [Experimento de race condition](#8-experimento-de-race-condition)
9. [Imagem no Docker Hub](#9-imagem-no-docker-hub)
10. [Questões teóricas](#10-questões-teóricas)

---

## 1. Visão geral

Este trabalho implementa um sistema **cliente-servidor concorrente** em C,
usando **sockets TCP** e um **protocolo de aplicação textual próprio**.

Múltiplos clientes se conectam ao servidor e operam sobre um estoque
compartilhado. O trabalho demonstra, na prática, como o acesso concorrente
a um recurso compartilhado introduz **race conditions** e como corrigi-las
com **mutex**.

### Binários produzidos

| Binário          | Descrição                                              |
|------------------|--------------------------------------------------------|
| `server`         | Versão B — com `pthread_mutex_t` (sincronizado)        |
| `server_unsafe`  | Versão A — sem mutex (vulnerável a race condition)     |
| `client`         | Cliente interativo via terminal                        |

---

## 2. Estrutura do repositório
bcc264-tp2/
├── src/
│   ├── server.c           # servidor concorrente — versão B (com mutex)
│   ├── server_unsafe.c    # servidor concorrente — versão A (sem mutex)
│   └── client.c           # cliente TCP interativo
├── include/
│   └── protocol.h         # defines, structs e macros do protocolo
├── Makefile               # compila os três binários
├── Dockerfile             # build em dois estágios
├── docker-compose.yml     # orquestra servidor + clientes
└── README.md              # este arquivo

---

## 3. Protocolo de aplicação

O protocolo é **textual**, executado sobre TCP.
Cada mensagem — comando ou resposta — termina com `\n`.

### 3.1 Comandos (cliente → servidor)

| Comando              | Descrição                          |
|----------------------|------------------------------------|
| `LIST`               | Lista todos os itens e quantidades |
| `BUY <item> <qtd>`   | Compra/reserva uma quantidade      |
| `CANCEL <item> <qtd>`| Devolve/cancela uma quantidade     |
| `STATUS <item>`      | Consulta a quantidade de um item   |
| `EXIT`               | Encerra a conexão                  |

### 3.2 Respostas (servidor → cliente)

| Situação                  | Resposta                      |
|---------------------------|-------------------------------|
| Operação bem-sucedida     | `OK <item> <qtd_atual>`       |
| Item não encontrado       | `ERROR item_not_found`        |
| Estoque insuficiente      | `ERROR insufficient_stock`    |
| Quantidade inválida       | `ERROR invalid_quantity`      |
| Comando desconhecido      | `ERROR unknown_command`       |
| Fim da listagem (LIST)    | `END`                         |
| Resposta ao EXIT          | `BYE`                         |

### 3.3 Exemplo de sessão

LIST
< cadeira 5
< mesa 3
< monitor 1
< END
BUY monitor 1
< OK monitor 0
BUY monitor 1
< ERROR insufficient_stock
STATUS cadeira
< OK cadeira 5
CANCEL cadeira 2
< OK cadeira 7
EXIT
< BYE


---

## 4. Pré-requisitos

### Execução local

| Ferramenta | Versão mínima |
|------------|---------------|
| gcc        | 11            |
| make       | 4.3           |
| Linux      | qualquer      |

### Execução via Docker

| Ferramenta     | Versão mínima |
|----------------|---------------|
| Docker Engine  | 24            |
| Docker Compose | 2.20          |

---

## 5. Compilação local

```bash
# Clona o repositório
git clone https://github.com/<usuario>/bcc264-tp2.git
cd bcc264-tp2

# Compila os três binários
make all

# Verifica os binários gerados
ls -lh server server_unsafe client
```

### Targets disponíveis no Makefile

```bash
make all            # compila server, server_unsafe e client
make server         # compila só o servidor com mutex
make server_unsafe  # compila só o servidor sem mutex
make client         # compila só o cliente
make clean          # remove os binários gerados
```

---

## 6. Execução local

### 6.1 Servidor com sincronização (versão B)

```bash
# Terminal 1
./server 9090
```
[SERVER] Escutando na porta 9090...
[SERVER] Estoque inicial:
cadeira      5
mesa         3
monitor      1

### 6.2 Cliente

```bash
# Terminal 2
./client 127.0.0.1 9090
```

### 6.3 Servidor sem sincronização (versão A)

```bash
# Terminal 1
./server_unsafe 9090
```
[SERVER UNSAFE] Escutando na porta 9090...
[SERVER UNSAFE] *** SEM SINCRONIZACAO — apenas para experimento ***

---

## 7. Execução via Docker

### 7.1 Build e subida com docker compose

```bash
# Build da imagem e subida do servidor + cliente
docker compose up --build
```

O `client` abre um terminal interativo conectado ao `tp2-server`
dentro da rede virtual `bcc264-net`.

### 7.2 Abrir um cliente adicional

Em outro terminal, com o compose já rodando:

```bash
docker compose run --rm client
```

### 7.3 Usar o servidor unsafe via compose

```bash
SERVER_BIN=./server_unsafe docker compose up --build
```

### 7.4 Execução manual com docker run

```bash
# Cria a rede virtual
docker network create bcc264-net

# Sobe o servidor
docker run -d \
  --name tp2-server \
  --network bcc264-net \
  -p 9090:9090 \
  usuario/bcc264-tp2:latest \
  ./server 9090

# Conecta um cliente
docker run -it --rm \
  --name tp2-client \
  --network bcc264-net \
  usuario/bcc264-tp2:latest \
  ./client tp2-server 9090
```

> O cliente referencia o servidor pelo hostname `tp2-server`,
> resolvido pela rede interna do Docker — não usa `127.0.0.1`.

### 7.5 Testar com netcat (sem o binário cliente)

```bash
nc 127.0.0.1 9090
LIST
BUY monitor 1
EXIT
```

---

## 8. Experimento de race condition

### 8.1 Objetivo

Demonstrar que dois clientes comprando o mesmo item simultaneamente,
sem sincronização, podem ambos receber `OK` mesmo com apenas
uma unidade disponível.

### 8.2 Passo a passo — versão A (sem mutex)

```bash
# Terminal 1 — servidor unsafe
./server_unsafe 9090

# Terminal 2 — cliente A
./client 127.0.0.1 9090

# Terminal 3 — cliente B
./client 127.0.0.1 9090
```

Nos terminais 2 e 3, envie quase simultaneamente:
BUY monitor 1

**Resultado esperado com race condition:**
Cliente A recebe:
< OK monitor 0
Cliente B recebe:
< OK monitor -1     ← estoque negativo — bug confirmado

### 8.3 Passo a passo — versão B (com mutex)

```bash
# Terminal 1 — servidor com mutex
./server 9090
```

Repita o mesmo experimento nos terminais 2 e 3.

**Resultado esperado com sincronização:**
Cliente A recebe:
< OK monitor 0      ← compra concluída
Cliente B recebe:
< ERROR insufficient_stock  ← corretamente rejeitado

### 8.4 Reprodução via Docker (profile race)

```bash
# Sobe servidor unsafe + dois clientes simultaneamente
SERVER_BIN=./server_unsafe docker compose --profile race up --build
```

Em seguida, em dois terminais separados, acesse cada cliente:

```bash
# Acessa o cliente 1
docker attach tp2-client

# Acessa o cliente 2
docker attach tp2-client2
```

---

## 9. Imagem no Docker Hub

### 9.1 Informações da imagem

| Campo       | Valor                              |
|-------------|------------------------------------|
| Usuário     | `<usuario>`                        |
| Imagem      | `bcc264-tp2`                       |
| Tag         | `latest`                           |
| URL         | `docker.io/<usuario>/bcc264-tp2`   |

### 9.2 Comandos para o avaliador

```bash
# Baixa a imagem do Docker Hub
docker pull usuario/bcc264-tp2:latest

# Executa o servidor com mutex
docker run -d \
  --name tp2-server \
  -p 9090:9090 \
  usuario/bcc264-tp2:latest \
  ./server 9090

# Executa o cliente
docker run -it --rm \
  usuario/bcc264-tp2:latest \
  ./client <IP-do-host> 9090

# Ou sobe tudo com compose
docker compose up
```

### 9.3 Verificar os binários dentro da imagem

```bash
docker run --rm usuario/bcc264-tp2:latest ls -lh /app
```
-rwxr-xr-x  server
-rwxr-xr-x  server_unsafe
-rwxr-xr-x  client

---
