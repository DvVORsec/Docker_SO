/*
 * client.c — Cliente TCP interativo
 * BCC264 - Sistemas Operacionais — DECOM/UFOP
 *
 * Conecta ao servidor, permite envio de comandos pelo terminal
 * e exibe as respostas recebidas.
 *
 * Compilar: gcc -Wall -o client client.c
 * Uso:      ./client <host> <porta>
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>        /* gethostbyname — resolve hostname do Docker */

/* ─────────────────────────────────────────────
 * Constantes
 * ───────────────────────────────────────────── */
#define BUF_LEN     256
#define NAME_LEN    32

/* ─────────────────────────────────────────────
 * Exibe ajuda dos comandos disponíveis
 * ───────────────────────────────────────────── */
static void print_help(void)
{
    printf("\n");
    printf("  Comandos disponíveis:\n");
    printf("  ──────────────────────────────────────\n");
    printf("  LIST               — lista todos os itens\n");
    printf("  BUY  <item> <qtd>  — compra/reserva quantidade\n");
    printf("  CANCEL <item> <qtd>— devolve/cancela quantidade\n");
    printf("  STATUS <item>      — consulta quantidade de um item\n");
    printf("  EXIT               — encerra a conexão\n");
    printf("  help               — exibe esta ajuda\n");
    printf("  ──────────────────────────────────────\n\n");
}

/* ─────────────────────────────────────────────
 * Recebe a resposta completa do servidor.
 *
 * O servidor encerra respostas com '\n'.
 * Para LIST, envia várias linhas até "END\n".
 * Para os demais comandos, uma única linha basta.
 *
 * Retorna 0 em sucesso, -1 se a conexão caiu.
 * ───────────────────────────────────────────── */
static int receive_response(int server_fd, const char *cmd_sent)
{
    char buf[BUF_LEN];
    ssize_t n;

    /*
     * Se o comando enviado foi LIST, o servidor responde com
     * múltiplas linhas terminadas por "END\n".
     * Para os demais, basta ler até o primeiro '\n'.
     */
    int is_list = (strncmp(cmd_sent, "LIST", 4) == 0);

    while (1) {
        memset(buf, 0, sizeof(buf));

        n = recv(server_fd, buf, sizeof(buf) - 1, 0);

        if (n <= 0) {
            /* servidor fechou a conexão ou erro de rede */
            printf("\n[!] Servidor desconectou.\n");
            return -1;
        }

        buf[n] = '\0';

        /* Imprime a resposta recebida */
        printf("< %s", buf);

        /* Para LIST, continua até receber a sentinela "END" */
        if (is_list) {
            if (strstr(buf, "END") != NULL)
                break;
        } else {
            break; /* resposta de linha única recebida */
        }
    }

    return 0;
}

/* ─────────────────────────────────────────────
 * Valida sintaticamente o comando antes de enviar.
 * Evita round-trips desnecessários ao servidor
 * por erros de digitação simples.
 *
 * Retorna 1 se válido, 0 se inválido.
 * ───────────────────────────────────────────── */
static int validate_cmd(const char *cmd)
{
    char op[BUF_LEN], item[NAME_LEN];
    int  qty;

    if (strcmp(cmd, "LIST") == 0)   return 1;
    if (strcmp(cmd, "EXIT") == 0)   return 1;

    if (sscanf(cmd, "BUY %31s %d", item, &qty) == 2 && qty > 0)
        return 1;

    if (sscanf(cmd, "CANCEL %31s %d", item, &qty) == 2 && qty > 0)
        return 1;

    if (sscanf(cmd, "STATUS %31s", item) == 1)
        return 1;

    return 0;
}

/* ─────────────────────────────────────────────
 * Resolve hostname ou IP e preenche sockaddr_in.
 * Necessário para funcionar dentro do Docker,
 * onde o servidor é referenciado por hostname
 * (ex: "tp2-server") e não por 127.0.0.1.
 *
 * Retorna 0 em sucesso, -1 em falha.
 * ───────────────────────────────────────────── */
static int resolve_address(const char *host, int port,
                           struct sockaddr_in *out_addr)
{
    struct hostent *he = gethostbyname(host);
    if (!he) {
        fprintf(stderr, "[!] Não foi possível resolver host '%s'\n", host);
        return -1;
    }

    memset(out_addr, 0, sizeof(*out_addr));
    out_addr->sin_family = AF_INET;
    out_addr->sin_port   = htons(port);
    memcpy(&out_addr->sin_addr, he->h_addr_list[0], he->h_length);

    return 0;
}

/* ─────────────────────────────────────────────
 * main
 * ───────────────────────────────────────────── */
int main(int argc, char *argv[])
{
    if (argc != 3) {
        fprintf(stderr, "Uso: %s <host> <porta>\n", argv[0]);
        fprintf(stderr, "  Ex (local):  %s 127.0.0.1 9090\n", argv[0]);
        fprintf(stderr, "  Ex (Docker): %s tp2-server 9090\n", argv[0]);
        return EXIT_FAILURE;
    }

    const char *host = argv[1];
    int port = atoi(argv[2]);

    if (port <= 0 || port > 65535) {
        fprintf(stderr, "[!] Porta inválida: %s\n", argv[2]);
        return EXIT_FAILURE;
    }

    /* ── 1. Resolver endereço do servidor ── */
    struct sockaddr_in server_addr;
    if (resolve_address(host, port, &server_addr) != 0)
        return EXIT_FAILURE;

    /* ── 2. Criar socket TCP ── */
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        perror("socket");
        return EXIT_FAILURE;
    }

    /* ── 3. Conectar ao servidor ── */
    if (connect(server_fd,
                (struct sockaddr *)&server_addr,
                sizeof(server_addr)) < 0) {
        perror("connect");
        close(server_fd);
        return EXIT_FAILURE;
    }

    printf("[CLIENT] Conectado a %s:%d\n", host, port);
    print_help();

    /* ── 4. Loop de entrada do usuário ── */
    char input[BUF_LEN];

    while (1) {
        printf("> ");
        fflush(stdout);

        /* Lê linha do terminal */
        if (fgets(input, sizeof(input), stdin) == NULL) {
            /* EOF (Ctrl+D) — encerra normalmente */
            printf("\n[CLIENT] EOF detectado. Encerrando.\n");
            break;
        }

        /* Remove '\n' do final */
        input[strcspn(input, "\r\n")] = '\0';

        /* Ignora linha vazia */
        if (strlen(input) == 0)
            continue;

        /* Comando local: help */
        if (strcmp(input, "help") == 0) {
            print_help();
            continue;
        }

        /* Validação local antes de enviar */
        if (!validate_cmd(input)) {
            printf("[!] Comando inválido. Digite 'help' para ajuda.\n");
            continue;
        }

        /* ── 5. Envia comando ao servidor ── */
        char to_send[BUF_LEN];
        snprintf(to_send, sizeof(to_send), "%s\n", input); /* adiciona '\n' */

        ssize_t sent = send(server_fd, to_send, strlen(to_send), 0);
        if (sent < 0) {
            perror("send");
            break;
        }

        /* ── 6. Recebe e exibe a resposta ── */
        if (receive_response(server_fd, input) < 0)
            break;

        /* EXIT: servidor respondeu BYE, encerramos o cliente */
        if (strcmp(input, "EXIT") == 0)
            break;
    }

    close(server_fd);
    printf("[CLIENT] Conexão encerrada.\n");
    return EXIT_SUCCESS;
}
