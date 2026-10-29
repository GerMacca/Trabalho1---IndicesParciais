#include <stdint.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Tamanho dos campos de texto (completados com espaços). */
#define TAM_CATEGORIA 40
#define TAM_MARCA     32   /* maior marca do dataset tem 32 caracteres */
#define TAM_DATA      19   /* "AAAA-MM-DD hh:mm:ss" */
#define TAM_EVENTO    16   /* "remove_from_cart" tem 16 caracteres */
#define TAM_SESSAO    36   /* UUID */

#define STATUS_ATIVO    ' '
#define STATUS_REMOVIDO '*'

/* Convenção usada pelo módulo tabela para qualquer registro:
     - os 8 primeiros bytes são a chave (uint64_t);
     - o penúltimo byte é o status (' ' ativo, '*' removido);
     - o último byte é '\n'.
   pack(1) garante que não há bytes de alinhamento entre os campos, então
   o tamanho do registro é a soma dos campos. */
#pragma pack(push, 1)

typedef struct {
    uint64_t product_id;                 /* chave */
    uint64_t category_id;
    char     category_code[TAM_CATEGORIA];
    char     brand[TAM_MARCA];
    float    price;
    char     status;
    char     fim;                        /* '\n' */
} Produto;                               /* 94 bytes */

typedef struct {
    uint64_t id;                         /* chave sequencial gerada na importação */
    char     event_time[TAM_DATA];
    char     event_type[TAM_EVENTO];
    uint64_t product_id;
    uint64_t user_id;
    char     user_session[TAM_SESSAO];
    char     status;
    char     fim;                        /* '\n' */
} Acesso;                                /* 97 bytes */

#pragma pack(pop)

void produto_cabecalho(void);
void produto_imprimir(const void *reg);
void acesso_cabecalho(void);
void acesso_imprimir(const void *reg);

/* Converte uma linha do CSV do dataset em um acesso e um produto.
   Formato: event_time,event_type,product_id,category_id,category_code,
            brand,price,user_id,user_session
   A linha é modificada. Retorna 1 se a linha é válida. */
int csv_converter_linha(char *linha, uint64_t id, Acesso *a, Produto *p);

#define CAMPOS_CSV 9

static const char *ou_traco(const char *s)
{
    return s[0] ? s : "-";
}

void produto_cabecalho(void)
{
    printf("  %-20s %-20s %-40s %-32s %10s\n",
           "product_id", "category_id", "category_code", "brand", "price");
}

void produto_imprimir(const void *reg)
{
    const Produto *p = reg;
    char categoria[TAM_CATEGORIA + 1], marca[TAM_MARCA + 1];

    texto_ler(categoria, p->category_code, TAM_CATEGORIA);
    texto_ler(marca, p->brand, TAM_MARCA);
    printf("%c %-20" PRIu64 " %-20" PRIu64 " %-40s %-32s %10.2f\n",
           p->status, p->product_id, p->category_id,
           ou_traco(categoria), ou_traco(marca), p->price);
}

void acesso_cabecalho(void)
{
    printf("  %-10s %-19s %-16s %-20s %-12s %-36s\n",
           "id", "event_time", "event_type", "product_id", "user_id", "user_session");
}

void acesso_imprimir(const void *reg)
{
    const Acesso *a = reg;
    char data[TAM_DATA + 1], evento[TAM_EVENTO + 1], sessao[TAM_SESSAO + 1];

    texto_ler(data, a->event_time, TAM_DATA);
    texto_ler(evento, a->event_type, TAM_EVENTO);
    texto_ler(sessao, a->user_session, TAM_SESSAO);
    printf("%c %-10" PRIu64 " %-19s %-16s %-20" PRIu64 " %-12" PRIu64 " %-36s\n",
           a->status, a->id, data, evento, a->product_id, a->user_id, ou_traco(sessao));
}

int csv_converter_linha(char *linha, uint64_t id, Acesso *a, Produto *p)
{
    char *campo[CAMPOS_CSV];
    char *c;
    int n = 0;

    linha[strcspn(linha, "\r\n")] = '\0';

    /* separa os campos (strtok não serve: há campos vazios) */
    campo[n++] = linha;
    for (c = linha; *c && n < CAMPOS_CSV; c++) {
        if (*c == ',') {
            *c = '\0';
            campo[n++] = c + 1;
        }
    }
    if (n < CAMPOS_CSV)
        return 0;

    memset(a, 0, sizeof *a);
    a->id = id;
    texto_fixo(a->event_time, TAM_DATA, campo[0]);   /* descarta o " UTC" */
    texto_fixo(a->event_type, TAM_EVENTO, campo[1]);
    a->product_id = strtoull(campo[2], NULL, 10);
    a->user_id = strtoull(campo[7], NULL, 10);
    texto_fixo(a->user_session, TAM_SESSAO, campo[8]);
    a->status = STATUS_ATIVO;
    a->fim = '\n';

    memset(p, 0, sizeof *p);
    p->product_id = a->product_id;
    p->category_id = strtoull(campo[3], NULL, 10);
    texto_fixo(p->category_code, TAM_CATEGORIA, campo[4]);
    texto_fixo(p->brand, TAM_MARCA, campo[5]);
    p->price = strtof(campo[6], NULL);
    p->status = STATUS_ATIVO;
    p->fim = '\n';

    return a->product_id != 0;
}
