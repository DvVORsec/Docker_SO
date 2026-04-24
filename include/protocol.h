/*
 * protocol.h — Protocolo de aplicação do servidor de estoque
 * BCC264 - Sistemas Operacionais — DECOM/UFOP
 *
 * Este header centraliza todas as convenções do protocolo textual
 * definido pelo grupo. Deve ser incluído por server.c,
 * server_unsafe.c e client.c.
 *
 * O protocolo roda sobre TCP. O TCP transporta os bytes —
 * o significado de cada mensagem é definido exclusivamente aqui.
 */

#ifndef PROTOCOL_H
#define PROTOCOL_H

/* ═════════════════════════════════════════════════════════════════
 * 1. PARÂMETROS GERAIS
 * ═════════════════════════════════════════════════════════════════ */

#define PROTO_PORT_DEFAULT  9090    /* porta padrão do servidor        */
#define PROTO_BUF_LEN       256     /* tamanho máximo de uma mensagem  */
#define PROTO_NAME_LEN      32      /* tamanho máximo do nome de item  */
#define PROTO_MAX_ITEMS     16      /* máximo de itens no estoque      */
#define PROTO_BACKLOG       8       /* fila de conexões pendentes      */

/*
 * Delimitador de mensagem.
 * Toda mensagem — comando ou resposta — termina com '\n'.
 * O receptor deve ler até encontrar este caractere.
 */
#define PROTO_DELIMITER     "\n"
#define PROTO_DELIMITER_C   '\n'

/* ═════════════════════════════════════════════════════════════════
 * 2. COMANDOS DO CLIENTE → SERVIDOR
 *
 * Sintaxe textual — enviada como string terminada em '\n'.
 *
 *   LIST
 *   BUY    <item> <qtd>
 *   CANCEL <item> <qtd>
 *   STATUS <item>
 *   EXIT
 *
 * Regras:
 *   - Campos separados por espaço simples.
 *   - <item> é case-sensitive, sem espaços.
 *   - <qtd> é inteiro positivo (> 0).
 *   - Nenhum campo opcional é aceito além dos listados.
 * ═════════════════════════════════════════════════════════════════ */

#define CMD_LIST    "LIST"      /* sem argumentos                      */
#define CMD_BUY     "BUY"       /* BUY <item> <qtd>                    */
#define CMD_CANCEL  "CANCEL"    /* CANCEL <item> <qtd>                 */
#define CMD_STATUS  "STATUS"    /* STATUS <item>                       */
#define CMD_EXIT    "EXIT"      /* sem argumentos — encerra conexão    */

/* ═════════════════════════════════════════════════════════════════
 * 3. RESPOSTAS DO SERVIDOR → CLIENTE
 *
 * Toda resposta segue o formato:
 *
 *   <STATUS> [campos adicionais]\n
 *
 * onde <STATUS> é OK ou ERROR.
 * ═════════════════════════════════════════════════════════════════ */

/* Prefixos de status */
#define RESP_OK     "OK"        /* operação bem-sucedida  */
#define RESP_ERROR  "ERROR"     /* operação falhou        */

/* ─────────────────────────────────────────────
 * 3.1 Respostas de sucesso — formato completo
 * ─────────────────────────────────────────────
 *
 *  LIST    → linhas "OK <item> <qtd_atual>" por item,
 *            seguidas de linha sentinela "END"
 *
 *  BUY     → "OK <item> <qtd_restante>"
 *              ex: OK monitor 0
 *
 *  CANCEL  → "OK <item> <qtd_atual>"
 *              ex: OK cadeira 6
 *
 *  STATUS  → "OK <item> <qtd_atual>"
 *              ex: OK mesa 3
 *
 *  EXIT    → "BYE"
 */
#define RESP_BYE        "BYE"   /* resposta ao EXIT                    */
#define RESP_LIST_END   "END"   /* sentinela de fim da listagem        */

/* ─────────────────────────────────────────────
 * 3.2 Códigos de erro — segundo campo após ERROR
 * ─────────────────────────────────────────────
 *
 * Formato: "ERROR <codigo>"
 *   ex: ERROR insufficient_stock
 */
#define ERR_NOT_FOUND       "item_not_found"      /* item não existe no estoque  */
#define ERR_NO_STOCK        "insufficient_stock"  /* qtd disponível insuficiente */
#define ERR_BAD_QTD         "invalid_quantity"    /* qtd <= 0 ou não numérica    */
#define ERR_UNKNOWN_CMD     "unknown_command"     /* comando não reconhecido     */

/* ═════════════════════════════════════════════════════════════════
 * 4. TABELA RESUMIDA DO PROTOCOLO
 * ═════════════════════════════════════════════════════════════════
 *
 *  Comando enviado          Resposta esperada
 *  ─────────────────────────────────────────────────────────────
 *  LIST                     <item> <qtd>\n ... END\n
 *  BUY <item> <qtd>         OK <item> <qtd_restante>\n
 *                           ERROR insufficient_stock\n
 *                           ERROR item_not_found\n
 *                           ERROR invalid_quantity\n
 *  CANCEL <item> <qtd>      OK <item> <qtd_atual>\n
 *                           ERROR item_not_found\n
 *                           ERROR invalid_quantity\n
 *  STATUS <item>            OK <item> <qtd_atual>\n
 *                           ERROR item_not_found\n
 *  EXIT                     BYE\n
 *  <desconhecido>           ERROR unknown_command\n
 * ═════════════════════════════════════════════════════════════════ */

/* ═════════════════════════════════════════════════════════════════
 * 5. ESTRUTURA DE DADOS COMPARTILHADA
 *
 * Definida aqui para que server.c e server_unsafe.c
 * usem exatamente o mesmo layout de memória.
 * ═════════════════════════════════════════════════════════════════ */

typedef struct {
    char name[PROTO_NAME_LEN];  /* nome do item — ex: "monitor"    */
    int  qty;                   /* quantidade disponível (>= 0)    */
} Item;

/* ═════════════════════════════════════════════════════════════════
 * 6. MACROS AUXILIARES DE FORMATAÇÃO
 *
 * Padronizam a montagem das strings de resposta,
 * evitando snprintf espalhados pelo código.
 * ═════════════════════════════════════════════════════════════════ */

/*
 * PROTO_FMT_OK(buf, item, qty)
 * Preenche buf com "OK <item> <qty>\n"
 */
#define PROTO_FMT_OK(buf, item, qty) \
    snprintf((buf), PROTO_BUF_LEN, "%s %s %d\n", RESP_OK, (item), (qty))

/*
 * PROTO_FMT_ERROR(buf, code)
 * Preenche buf com "ERROR <code>\n"
 */
#define PROTO_FMT_ERROR(buf, code) \
    snprintf((buf), PROTO_BUF_LEN, "%s %s\n", RESP_ERROR, (code))

/*
 * PROTO_FMT_LIST_LINE(buf, item, qty)
 * Preenche buf com "<item> <qty>\n"  — linha individual do LIST
 */
#define PROTO_FMT_LIST_LINE(buf, item, qty) \
    snprintf((buf), PROTO_BUF_LEN, "%s %d\n", (item), (qty))

#endif /* PROTOCOL_H */
