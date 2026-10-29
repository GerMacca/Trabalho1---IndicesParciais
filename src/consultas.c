#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Pergunta 1: quais marcas são as mais compradas? Uma marca domina o
   mercado (mais de 50% das compras) ou há equilíbrio entre várias?
   Percorre os acessos e busca cada produto comprado pelo índice parcial. */
void consulta_marcas_mais_compradas(Tabela *produtos, Tabela *acessos, int top);

/* Pergunta 2: em que horário do dia (UTC) acontecem mais compras? */
void consulta_compras_por_hora(Tabela *acessos);

/* Pergunta 3: qual a taxa de conversão do funil
   visualização -> carrinho -> compra? */
void consulta_funil_conversao(Tabela *acessos);

/* ------------------------------------------------------------------ */
/* Contagem por marca: tabela hash pequena (uma entrada por marca, não  */
/* por registro), que cresce quando passa da metade.                   */
/* ------------------------------------------------------------------ */

typedef struct {
    char    marca[TAM_MARCA + 1];
    int64_t compras;
    double  receita;
    int     usado;
} ContMarca;

typedef struct {
    ContMarca *v;
    size_t     cap;
    size_t     qtd;
} TabelaMarcas;

static size_t hash_texto(const char *s)
{
    size_t h = 5381;

    while (*s)
        h = h * 33 + (unsigned char)*s++;
    return h;
}

static ContMarca *marca_buscar(TabelaMarcas *m, const char *marca)
{
    size_t i = hash_texto(marca) & (m->cap - 1);

    while (m->v[i].usado && strcmp(m->v[i].marca, marca) != 0)
        i = (i + 1) & (m->cap - 1);
    return &m->v[i];
}

static int marcas_crescer(TabelaMarcas *m)
{
    TabelaMarcas nova = {calloc(m->cap * 2, sizeof(ContMarca)), m->cap * 2, m->qtd};
    size_t i;

    if (!nova.v)
        return 0;
    for (i = 0; i < m->cap; i++)
        if (m->v[i].usado)
            *marca_buscar(&nova, m->v[i].marca) = m->v[i];
    free(m->v);
    *m = nova;
    return 1;
}

static int marcas_somar(TabelaMarcas *m, const char *marca, double preco)
{
    ContMarca *c;

    if (m->qtd * 2 >= m->cap && !marcas_crescer(m))
        return 0;
    c = marca_buscar(m, marca);
    if (!c->usado) {
        snprintf(c->marca, sizeof c->marca, "%s", marca);
        c->usado = 1;
        m->qtd++;
    }
    c->compras++;
    c->receita += preco;
    return 1;
}

static int cmp_compras_desc(const void *a, const void *b)
{
    int64_t x = ((const ContMarca *)a)->compras;
    int64_t y = ((const ContMarca *)b)->compras;

    return (x < y) - (x > y);
}

static int evento_igual(const Acesso *a, const char *tipo)
{
    char evento[TAM_EVENTO + 1];

    texto_ler(evento, a->event_type, TAM_EVENTO);
    return strcmp(evento, tipo) == 0;
}

void consulta_marcas_mais_compradas(Tabela *produtos, Tabela *acessos, int top)
{
    TabelaMarcas m = {calloc(1024, sizeof(ContMarca)), 1024, 0};
    Iterador it;
    Acesso a;
    Produto p;
    int64_t total = 0, sem_marca = 0, leituras_indice = 0;
    size_t i, k;

    if (!m.v || !iter_iniciar(&it, acessos)) {
        free(m.v);
        return;
    }

    while (iter_proximo(&it, &a)) {
        char marca[TAM_MARCA + 1];

        if (!evento_igual(&a, "purchase"))
            continue;
        total++;
        if (!tabela_busca_indice(produtos, a.product_id, &p)) {
            marcas_somar(&m, "(produto removido)", 0);
            continue;
        }
        leituras_indice += produtos->leituras;
        texto_ler(marca, p.brand, TAM_MARCA);
        if (!marca[0]) {
            sem_marca++;
            strcpy(marca, "(sem marca)");
        }
        marcas_somar(&m, marca, p.price);
    }
    iter_fim(&it);

    if (total == 0) {
        printf("Nenhuma compra registrada.\n");
        free(m.v);
        return;
    }

    /* compacta as entradas usadas e ordena pela quantidade de compras */
    for (i = 0, k = 0; i < m.cap; i++)
        if (m.v[i].usado)
            m.v[k++] = m.v[i];
    qsort(m.v, k, sizeof(ContMarca), cmp_compras_desc);

    printf("Compras: %" PRId64 "  |  marcas distintas: %u  |  compras sem marca: %" PRId64 "\n",
           total, (unsigned)k, sem_marca);
    printf("Media de leituras por busca no indice: %.1f\n\n",
           (double)leituras_indice / (double)total);
    printf("%-4s %-32s %10s %8s %14s\n", "#", "marca", "compras", "%", "receita (USD)");
    for (i = 0; i < k && i < (size_t)top; i++)
        printf("%-4u %-32s %10" PRId64 " %7.2f%% %14.2f\n", (unsigned)(i + 1),
               m.v[i].marca, m.v[i].compras,
               100.0 * (double)m.v[i].compras / (double)total, m.v[i].receita);

    for (i = 0; i < k && m.v[i].marca[0] == '('; i++)
        ;   /* primeira marca de verdade */
    if (i < k) {
        double lider = 100.0 * (double)m.v[i].compras / (double)total;
        printf("\nHipotese \"uma marca tem mais de 50%% das compras\": %s (%s com %.2f%%)\n",
               lider > 50.0 ? "CONFIRMADA" : "REJEITADA", m.v[i].marca, lider);
    }
    free(m.v);
}

void consulta_compras_por_hora(Tabela *acessos)
{
    int64_t compras[24] = {0}, maior = 0, total = 0;
    Iterador it;
    Acesso a;
    int h, pico = 0;

    if (!iter_iniciar(&it, acessos))
        return;
    while (iter_proximo(&it, &a)) {
        if (!evento_igual(&a, "purchase"))
            continue;
        h = (a.event_time[11] - '0') * 10 + (a.event_time[12] - '0');
        if (h >= 0 && h < 24) {
            compras[h]++;
            total++;
        }
    }
    iter_fim(&it);

    for (h = 0; h < 24; h++)
        if (compras[h] > maior) {
            maior = compras[h];
            pico = h;
        }

    printf("Compras por hora do dia (UTC), total %" PRId64 ":\n", total);
    for (h = 0; h < 24; h++) {
        int barra = maior ? (int)(50 * compras[h] / maior) : 0;
        printf("%02dh %8" PRId64 " |%.*s\n", h, compras[h], barra,
               "##################################################");
    }
    if (total > 0)
        printf("\nHorario de pico: %02dh-%02dh UTC (%.2f%% das compras)\n",
               pico, pico + 1, 100.0 * (double)maior / (double)total);
}

void consulta_funil_conversao(Tabela *acessos)
{
    static const char *tipos[] = {"view", "cart", "remove_from_cart", "purchase"};
    int64_t qtd[4] = {0}, outros = 0, total = 0;
    Iterador it;
    Acesso a;
    int i;

    if (!iter_iniciar(&it, acessos))
        return;
    while (iter_proximo(&it, &a)) {
        total++;
        for (i = 0; i < 4 && !evento_igual(&a, tipos[i]); i++)
            ;
        if (i < 4)
            qtd[i]++;
        else
            outros++;
    }
    iter_fim(&it);

    printf("Eventos: %" PRId64 "\n", total);
    for (i = 0; i < 4; i++)
        printf("  %-18s %10" PRId64 " (%6.2f%%)\n", tipos[i], qtd[i],
               total ? 100.0 * (double)qtd[i] / (double)total : 0.0);
    if (outros)
        printf("  %-18s %10" PRId64 "\n", "outros", outros);

    if (qtd[0] > 0) {
        printf("\nVisualizacao -> carrinho: %6.2f%%\n", 100.0 * (double)qtd[1] / (double)qtd[0]);
        printf("Visualizacao -> compra .: %6.2f%%\n", 100.0 * (double)qtd[3] / (double)qtd[0]);
    }
    if (qtd[1] > 0)
        printf("Carrinho -> compra .....: %6.2f%%\n", 100.0 * (double)qtd[3] / (double)qtd[1]);
}
