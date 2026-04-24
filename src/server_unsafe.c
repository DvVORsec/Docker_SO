/*
 * server_unsafe.c — Servidor TCP concorrente SEM sincronização
 * BCC264 - Sistemas Operacionais — DECOM/UFOP
 *
 * Versão A: sem mutex — propositalmente vulnerável a race condition
 * Use este binário apenas para demonstrar o problema no experimento.
 *
 * Compilar: gcc -Wall -pthread -o server_unsafe server_unsafe.c
 * Uso:      ./server_unsafe <porta>
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <pthread.h>

/* ─────────────────────────────────────────────
 * Protocolo de aplicação — comandos e respostas
 * ───────────────────────────────────────────── */
#define CMD_LIST    "LIST"
#define CMD_BUY     "BUY"
#define CMD_CANCEL  "CANCEL"
#define CMD_STATUS  "STATUS"
#define CMD_EXIT    "EXIT"

#define RESP_OK     "OK"
#define RESP_ERROR  "ERROR"

#define MSG_NO_STOCK    "insufficient_stock"
#define MSG_NOT_FOUND   "item_not_found"
#define MSG_BAD_QTD     "invalid_quantity"
#define MSG_UNKNOWN     "unknown_command"

/* ─────────────────────────────────────────────
 * Estoque compartilhado
 * ───────────────────────────────────────────── */
#define MAX_ITEMS   16
#define NAME_LEN    32
#define BUF_LEN     256
#define BACKLOG     8

typedef struct {
    char name[NAME_LEN];
    int  qty;
} Item;

/* Estado global compartilhado entre todas as threads */
static Item stock[MAX_ITEMS];
static int  stock_count = 0;

/*
 * AUSÊNCIA INTENCIONAL: nenhum pthread_mutex_t declarado aqui.
 * O acesso ao estoque é feito sem qualquer proteção.
 */

/* ─────────────────────────────────────────────
 * Inicialização do estoque
 * ───────────────────────────────────────────── */
static void init_stock(void)
{
    strcpy(stock[0].name, "cadeira");  stock[0].qty = 5;
    strcpy(stock[1].name, "mesa");     stock[1].qty = 3;
    strcpy(stock[2].name, "monitor");  stock[2].qty = 1; /* qty=1: ideal para
                                                            demonstrar a race */
    stock_count = 3;
}

/* ─────────────────────────────────────────────
 * Busca um item pelo nome; retorna índice ou -1
 * ───────────────────────────────────────────── */
static int find_item(const char *name)
{
    for (int i = 0; i < stock_count; i++) {
        if (strcmp(stock[i].name, name) == 0)
            return i;
    }
    return -1;
}

/* ─────────────────────────────────────────────
 * Handlers de cada comando — SEM mutex
 * ───────────────────────────────────────────── */

/* LIST */
static void handle_list(int client_fd)
{
    char line[BUF_LEN];

    /*
     * UNSAFE: outra thread pode estar modificando stock[]
     * enquanto iteramos aqui — leitura inconsistente possível.
     */
    for (int i = 0; i < stock_count; i++) {
        snprintf(line, sizeof(line), "%s %d\n",
                 stock[i].name, stock[i].qty);
        send(client_fd, line, strlen(line), 0);
    }

    send(client_fd, "END\n", 4, 0);
}

/* BUY <item> <qtd> */
static void handle_buy(int client_fd, const char *item_name, int qty)
{
    char resp[BUF_LEN];

    if (qty <= 0) {
        snprintf(resp, sizeof(resp), "%s %s\n", RESP_ERROR, MSG_BAD_QTD);
        send(client_fd, resp, strlen(resp), 0);
        return;
    }

    /*
     * !! REGIÃO CRÍTICA DESPROTEGIDA !!
     *
     * Sequência vulnerável:
     *   1. Thread A lê stock[idx].qty == 1  ← preempção aqui
     *   2. Thread B lê stock[idx].qty == 1
     *   3. Thread B valida (1 >= 1) → decrementa → qty == 0
     *   4. Thread A valida (1 >= 1) → decrementa → qty == -1
     *   5. Ambas respondem OK — estoque fica negativo.
     *
     * Para ampliar a janela de race durante o experimento,
     * o sleep abaixo simula um pequeno atraso entre leitura
     * e escrita, tornando o problema mais fácil de reproduzir.
     */

    int idx = find_item(item_name);

    if (idx == -1) {
        snprintf(resp, sizeof(resp), "%s %s\n", RESP_ERROR, MSG_NOT_FOUND);
        send(client_fd, resp, strlen(resp), 0);
        return;
    }

    /* Leitura do estoque */
    int current = stock[idx].qty;

    /*
     * usleep: abre artificialmente a janela de race condition.
     * Remova esta linha em produção — existe aqui apenas para
     * fins didáticos e para tornar o bug reprodutível no experimento.
     */
    usleep(10000); /* 10 ms de janela entre leitura e escrita */

    /* Validação e escrita — não atômicas com a leitura acima */
    if (current < qty) {
        snprintf(resp, sizeof(resp), "%s %s\n", RESP_ERROR, MSG_NO_STOCK);
    } else {
        stock[idx].qty -= qty; /* escrita sem proteção */
        snprintf(resp, sizeof(resp), "%s %s %d\n",
                 RESP_OK, item_name, stock[idx].qty);
        printf("[BUY-UNSAFE] %s: -%d → estoque=%d\n",
               item_name, qty, stock[idx].qty);
    }

    send(client_fd, resp, strlen(resp), 0);
}

/* CANCEL <item> <qtd> */
static void handle_cancel(int client_fd, const char *item_name, int qty)
{
    char resp[BUF_LEN];

    if (qty <= 0) {
        snprintf(resp, sizeof(resp), "%s %s\n", RESP_ERROR, MSG_BAD_QTD);
        send(client_fd, resp, strlen(resp), 0);
        return;
    }

    /*
     * UNSAFE: incremento sem proteção.
     * Duas threads cancelando ao mesmo tempo podem gerar
     * quantidade final incorreta por escrita concorrente.
     */
    int idx = find_item(item_name);

    if (idx == -1) {
        snprintf(resp, sizeof(resp), "%s %s\n", RESP_ERROR, MSG_NOT_FOUND);
    } else {
        stock[idx].qty += qty; /* sem proteção */
        snprintf(resp, sizeof(resp), "%s %s %d\n",
                 RESP_OK, item_name, stock[idx].qty);
        printf("[CANCEL-UNSAFE] %s: +%d → estoque=%d\n",
               item_name, qty, stock[idx].qty);
    }

    send(client_fd, resp, strlen(resp), 0);
}

/* STATUS <item> */
static void handle_status(int client_fd, const char *item_name)
{
    char resp[BUF_LEN];

    /*
     * UNSAFE: leitura simples sem lock — pode ler valor
     * parcialmente atualizado por outra thread.
     */
    int idx = find_item(item_name);

    if (idx == -1) {
        snprintf(resp, sizeof(resp), "%s %s\n", RESP_ERROR, MSG_NOT_FOUND);
    } else {
        snprintf(resp, sizeof(resp), "%s %s %d\n",
                 RESP_OK, item_name, stock[idx].qty);
    }

    send(client_fd, resp, strlen(resp), 0);
}

/* ─────────────────────────────────────────────
 * Função da thread: interpreta comandos de um cliente
 * Idêntica ao server.c — a diferença está nos handlers.
 * ───────────────────────────────────────────── */
static void *client_handler(void *arg)
{
    int client_fd = *(int *)arg;
    free(arg);

    char buf[BUF_LEN];
    char cmd[BUF_LEN], item[NAME_LEN];
    int  qty;
    ssize_t n;

    printf("[+] Nova conexão — fd=%d\n", client_fd);

    while (1) {
        memset(buf, 0, sizeof(buf));

        n = recv(client_fd, buf, sizeof(buf) - 1, 0);

        if (n <= 0) {
            printf("[-] Conexão encerrada — fd=%d\n", client_fd);
            break;
        }

        buf[strcspn(buf, "\r\n")] = '\0';

        if (strlen(buf) == 0)
            continue;

        printf("[CMD fd=%d] '%s'\n", client_fd, buf);

        if (strcmp(buf, CMD_LIST) == 0) {
            handle_list(client_fd);

        } else if (sscanf(buf, "BUY %31s %d", item, &qty) == 2) {
            handle_buy(client_fd, item, qty);

        } else if (sscanf(buf, "CANCEL %31s %d", item, &qty) == 2) {
            handle_cancel(client_fd, item, qty);

        } else if (sscanf(buf, "STATUS %31s", item) == 1) {
            handle_status(client_fd, item);

        } else if (strcmp(buf, CMD_EXIT) == 0) {
            send(client_fd, "BYE\n", 4, 0);
            break;

        } else {
            char err[BUF_LEN];
            snprintf(err, sizeof(err), "%s %s\n", RESP_ERROR, MSG_UNKNOWN);
            send(client_fd, err, strlen(err), 0);
        }
    }

    close(client_fd);
    return NULL;
}

/* ─────────────────────────────────────────────
 * main — idêntico ao server.c
 * ───────────────────────────────────────────── */
int main(int argc, char *argv[])
{
    if (argc != 2) {
        fprintf(stderr, "Uso: %s <porta>\n", argv[0]);
        return EXIT_FAILURE;
    }

    int port = atoi(argv[1]);
    if (port <= 0 || port > 65535) {
        fprintf(stderr, "Porta inválida: %s\n", argv[1]);
        return EXIT_FAILURE;
    }

    init_stock();

    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        perror("socket");
        return EXIT_FAILURE;
    }

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr = {
        .sin_family      = AF_INET,
        .sin_port        = htons(port),
        .sin_addr.s_addr = INADDR_ANY
    };

    if (bind(server_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind");
        close(server_fd);
        return EXIT_FAILURE;
    }

    if (listen(server_fd, BACKLOG) < 0) {
        perror("listen");
        close(server_fd);
        return EXIT_FAILURE;
    }

    printf("[SERVER UNSAFE] Escutando na porta %d...\n", port);
    printf("[SERVER UNSAFE] *** SEM SINCRONIZACAO — apenas para experimento ***\n\n");
    printf("[SERVER UNSAFE] Estoque inicial:\n");
    for (int i = 0; i < stock_count; i++)
        printf("  %-12s %d\n", stock[i].name, stock[i].qty);
    printf("\n");

    while (1) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);

        int client_fd = accept(server_fd,
                               (struct sockaddr *)&client_addr,
                               &client_len);
        if (client_fd < 0) {
            perror("accept");
            continue;
        }

        printf("[ACCEPT] Cliente %s:%d — fd=%d\n",
               inet_ntoa(client_addr.sin_addr),
               ntohs(client_addr.sin_port),
               client_fd);

        pthread_t tid;

        int *fd_ptr = malloc(sizeof(int));
        if (!fd_ptr) {
            perror("malloc");
            close(client_fd);
            continue;
        }
        *fd_ptr = client_fd;

        if (pthread_create(&tid, NULL, client_handler, fd_ptr) != 0) {
            perror("pthread_create");
            close(client_fd);
            free(fd_ptr);
            continue;
        }

        pthread_detach(tid);
    }

    close(server_fd);
    return EXIT_SUCCESS;
}
