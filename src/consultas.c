#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Pergunta 1: quais marcas sao as mais compradas? Uma marca domina o
   mercado (mais de 50% das compras) ou ha equilibrio entre varias?
   Percorre os acessos e busca cada produto comprado pelo indice parcial */
void consulta_marcas_mais_compradas(Tabela *produtos, Tabela *acessos, int top);

/* Pergunta 2: em que horario do dia (UTC) acontecem mais compras?
   valores = 0 mostra grafico de barras; 1 mostra compras e porcentagem */
void consulta_compras_por_hora(Tabela *acessos, int valores);

/* Pergunta 3: qual a taxa de conversao do funil
   visualizacao -> carrinho -> compra? */
void consulta_funil_conversao(Tabela *acessos);

/* Pergunta extra 4: quais categorias geram mais faturamento?
   Usa o primeiro nivel do category_code (electronics.smartphone -> electronics) */
void consulta_categorias_faturamento(Tabela *produtos, Tabela *acessos);

/* Pergunta extra 5: em que dia da semana se compra mais?
   Compara a media de compras por dia e a conversao de cada dia da semana */
void consulta_dia_da_semana(Tabela *acessos);

// ------------------------------------------------------------------
// Contagem por marca: tabela hash pequena (uma entrada por marca, nao
// por registro), que cresce quando passa da metade
// ------------------------------------------------------------------

typedef struct {
    char   marca[TAM_MARCA + 1];
    long   compras;
    double receita;
    int    usado;
} ContMarca;

typedef struct {
    ContMarca *v;
    int        cap;
    int        qtd;
} TabelaMarcas;

/* Posicao da marca na tabela (o resto da divisao mantem o valor
   sempre entre 0 e cap - 1) */
static int hash_texto(const char *s, int cap)
{
    long h = 0;

    while (*s)
        h = (h * 31 + *s++) % cap;
    return h < 0 ? (int)(h + cap) : (int)h;
}

static ContMarca *marca_buscar(TabelaMarcas *m, const char *marca)
{
    int i = hash_texto(marca, m->cap);

    while (m->v[i].usado && strcmp(m->v[i].marca, marca) != 0)
        i = (i + 1) % m->cap;
    return &m->v[i];
}

static int marcas_crescer(TabelaMarcas *m)
{
    TabelaMarcas nova = {calloc(m->cap * 2, sizeof(ContMarca)), m->cap * 2, m->qtd};
    int i;

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

// Mais compras primeiro; em caso de empate, ordem alfabetica
static int cmp_compras_desc(const void *a, const void *b)
{
    const ContMarca *x = a, *y = b;

    if (x->compras != y->compras)
        return x->compras < y->compras ? 1 : -1;
    return strcmp(x->marca, y->marca);
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
    long total = 0, sem_marca = 0, leituras_indice = 0;
    int i, k;

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

    // compacta as entradas usadas e ordena pela quantidade de compras
    for (i = 0, k = 0; i < m.cap; i++)
        if (m.v[i].usado)
            m.v[k++] = m.v[i];
    qsort(m.v, k, sizeof(ContMarca), cmp_compras_desc);

    printf("Compras: %ld  |  marcas distintas: %d  |  compras sem marca: %ld\n",
           total, k, sem_marca);
    printf("Media de leituras por busca no indice: %.1f\n\n",
           (double)leituras_indice / (double)total);
    printf("%-4s %-32s %10s %8s %14s\n", "#", "marca", "compras", "%", "receita (USD)");
    for (i = 0; i < k && i < top; i++)
        printf("%-4d %-32s %10ld %7.2f%% %14.2f\n", i + 1,
               m.v[i].marca, m.v[i].compras,
               100.0 * (double)m.v[i].compras / (double)total, m.v[i].receita);

    for (i = 0; i < k && m.v[i].marca[0] == '('; i++)
        ;   // primeira marca de verdade
    if (i < k) {
        double lider = 100.0 * (double)m.v[i].compras / (double)total;
        printf("\nHipotese \"uma marca tem mais de 50%% das compras\": %s (%s com %.2f%%)\n",
               lider > 50.0 ? "CONFIRMADA" : "REJEITADA", m.v[i].marca, lider);
    }
    free(m.v);
}

void consulta_compras_por_hora(Tabela *acessos, int valores)
{
    long compras[24] = {0}, maior = 0, total = 0;
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

    printf("Compras por hora do dia (UTC), total %ld:\n", total);
    if (valores) {
        printf("\nHora  Compras        %%\n");
        for (h = 0; h < 24; h++)
            printf("%02dh  %8ld  %6.2f%%\n", h, compras[h],
                   total ? 100.0 * (double)compras[h] / (double)total : 0.0);
    } else {
        // o horario de pico tem 50 '#'; os outros, a proporcao disso
        for (h = 0; h < 24; h++) {
            int barra = maior ? (int)(50 * compras[h] / maior) : 0;
            printf("%02dh %8ld |%.*s\n", h, compras[h], barra,
                   "##################################################");
        }
    }
    if (total > 0)
        printf("\nHorario de pico: %02dh-%02dh UTC (%.2f%% das compras)\n",
               pico, pico + 1, 100.0 * (double)maior / (double)total);
}

void consulta_funil_conversao(Tabela *acessos)
{
    static const char *tipos[] = {"view", "cart", "remove_from_cart", "purchase"};
    long qtd[4] = {0}, outros = 0, total = 0;
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

    printf("Eventos: %ld\n", total);
    for (i = 0; i < 4; i++)
        printf("  %-18s %10ld (%6.2f%%)\n", tipos[i], qtd[i],
               total ? 100.0 * (double)qtd[i] / (double)total : 0.0);
    if (outros)
        printf("  %-18s %10ld\n", "outros", outros);

    if (qtd[0] > 0) {
        printf("\nVisualizacao -> carrinho: %6.2f%%\n", 100.0 * (double)qtd[1] / (double)qtd[0]);
        printf("Visualizacao -> compra .: %6.2f%%\n", 100.0 * (double)qtd[3] / (double)qtd[0]);
    }
    if (qtd[1] > 0)
        printf("Carrinho -> compra .....: %6.2f%%\n", 100.0 * (double)qtd[3] / (double)qtd[1]);
}

// ------------------------------------------------------------------
// Pergunta extra 4: faturamento por categoria
// ------------------------------------------------------------------

// Maior faturamento primeiro
static int cmp_receita_desc(const void *a, const void *b)
{
    const ContMarca *x = a, *y = b;

    if (x->receita != y->receita)
        return x->receita < y->receita ? 1 : -1;
    return strcmp(x->marca, y->marca);
}

void consulta_categorias_faturamento(Tabela *produtos, Tabela *acessos)
{
    TabelaMarcas m = {calloc(64, sizeof(ContMarca)), 64, 0};
    Iterador it;
    Acesso a;
    Produto p;
    long total = 0;
    double receita = 0;
    int i, k;

    if (!m.v || !iter_iniciar(&it, acessos)) {
        free(m.v);
        return;
    }

    /* mesma ideia da pergunta 1: para cada compra, busca o produto pelo
       indice parcial e soma na categoria dele (a tabela hash guarda uma
       entrada por categoria, nao por registro) */
    while (iter_proximo(&it, &a)) {
        char categoria[TAM_CATEGORIA + 1];
        char *ponto;

        if (!evento_igual(&a, "purchase"))
            continue;
        total++;
        if (!tabela_busca_indice(produtos, a.product_id, &p)) {
            marcas_somar(&m, "(produto removido)", 0);
            continue;
        }
        texto_ler(categoria, p.category_code, TAM_CATEGORIA);
        ponto = strchr(categoria, '.');
        if (ponto)
            *ponto = '\0';   // fica so o primeiro nivel
        if (!categoria[0])
            strcpy(categoria, "(sem categoria)");
        marcas_somar(&m, categoria, p.price);
        receita += p.price;
    }
    iter_fim(&it);

    if (total == 0) {
        printf("Nenhuma compra registrada.\n");
        free(m.v);
        return;
    }

    for (i = 0, k = 0; i < m.cap; i++)
        if (m.v[i].usado)
            m.v[k++] = m.v[i];
    qsort(m.v, k, sizeof(ContMarca), cmp_receita_desc);

    printf("Compras: %ld  |  faturamento: %.2f USD  |  categorias: %d\n\n",
           total, receita, k);
    printf("%-4s %-20s %10s %16s %8s %12s\n",
           "#", "categoria", "compras", "faturamento", "%", "ticket medio");
    for (i = 0; i < k; i++)
        printf("%-4d %-20s %10ld %16.2f %7.2f%% %12.2f\n", i + 1,
               m.v[i].marca, m.v[i].compras, m.v[i].receita,
               receita > 0 ? 100.0 * m.v[i].receita / receita : 0.0,
               m.v[i].compras ? m.v[i].receita / (double)m.v[i].compras : 0.0);
    free(m.v);
}

// ------------------------------------------------------------------
// Pergunta extra 5: compras por dia da semana
// ------------------------------------------------------------------

/* Dia da semana de uma data (0 = domingo ... 6 = sabado),
   pelo metodo de Sakamoto */
static int dia_da_semana(int ano, int mes, int dia)
{
    static const int t[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};

    if (mes < 3)
        ano--;
    return (ano + ano / 4 - ano / 100 + ano / 400 + t[mes - 1] + dia) % 7;
}

static int digitos(const char *s, int n)
{
    int v = 0, i;

    for (i = 0; i < n; i++)
        v = v * 10 + (s[i] - '0');
    return v;
}

void consulta_dia_da_semana(Tabela *acessos)
{
    static const char *nomes[] = {"domingo", "segunda", "terca", "quarta",
                                  "quinta", "sexta", "sabado"};
    long compras[7] = {0}, views[7] = {0}, dias[7] = {0};
    char data_anterior[11] = "";
    double media[7], util = 0, fds = 0;
    Iterador it;
    Acesso a;
    int d, pico = 0;

    if (!iter_iniciar(&it, acessos))
        return;
    while (iter_proximo(&it, &a)) {
        d = dia_da_semana(digitos(a.event_time, 4), digitos(a.event_time + 5, 2),
                          digitos(a.event_time + 8, 2));
        if (d < 0 || d > 6)
            continue;
        /* os acessos estao em ordem cronologica: quando a data muda, e um
           dia novo (conta quantos dias de cada tipo existem no periodo) */
        if (strncmp(a.event_time, data_anterior, 10) != 0) {
            dias[d]++;
            strncpy(data_anterior, a.event_time, 10);
        }
        if (evento_igual(&a, "purchase"))
            compras[d]++;
        else if (evento_igual(&a, "view"))
            views[d]++;
    }
    iter_fim(&it);

    printf("%-9s %6s %9s %16s %10s\n", "dia", "dias", "compras", "media por dia", "conversao");
    for (d = 0; d < 7; d++) {
        media[d] = dias[d] ? (double)compras[d] / (double)dias[d] : 0.0;
        if (media[d] > media[pico])
            pico = d;
        printf("%-9s %6ld %9ld %16.1f %9.2f%%\n", nomes[d], dias[d], compras[d], media[d],
               views[d] ? 100.0 * (double)compras[d] / (double)views[d] : 0.0);
    }

    for (d = 1; d <= 5; d++)
        util += media[d];
    fds = (media[0] + media[6]) / 2.0;
    util /= 5.0;
    printf("\nDia com mais compras (media por dia): %s\n", nomes[pico]);
    printf("Media por dia: dias uteis %.1f  |  fim de semana %.1f\n", util, fds);
}
