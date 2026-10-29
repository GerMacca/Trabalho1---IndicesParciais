#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <direct.h> 

int importar_csv(const char *caminho_csv, long limite,
                 long *n_acessos, long *n_produtos);

#define TAM_LINHA 1024
#define TAM_NOME  64

typedef char NomeArquivo[TAM_NOME];

typedef struct {
    NomeArquivo *nomes;
    int          qtd;
    int          capacidade;
    int          proximo_id;
} ListaRuns;

typedef struct {
    Produto p;
    int     ordem;
} ProdutoLido;

static int cmp_produto(const void *a, const void *b)
{
    const ProdutoLido *x = a, *y = b;

    if (x->p.product_id != y->p.product_id)
        return x->p.product_id > y->p.product_id ? 1 : -1;
    return x->ordem - y->ordem;
}

static const char *nova_run(ListaRuns *l)
{
    if (l->qtd == l->capacidade) {
        int nova = l->capacidade ? l->capacidade * 2 : 16;
        NomeArquivo *v = realloc(l->nomes, nova * sizeof *v);

        if (!v)
            return NULL;
        l->nomes = v;
        l->capacidade = nova;
    }
    snprintf(l->nomes[l->qtd], TAM_NOME, DIR_DADOS "run_%04d.tmp", l->proximo_id++);
    return l->nomes[l->qtd++];
}

static int gravar_run(ListaRuns *l, ProdutoLido *buf, int n)
{
    const char *nome = nova_run(l);
    FILE *f;
    int i;

    if (!nome || !(f = fopen(nome, "wb")))
        return 0;
    qsort(buf, n, sizeof *buf, cmp_produto);
    for (i = 0; i < n; i++)
        if (i == 0 || buf[i].p.product_id != buf[i - 1].p.product_id)
            fwrite(&buf[i].p, sizeof(Produto), 1, f);
    fclose(f);
    return 1;
}

/* Intercala ate MAX_VIAS ordenadas em uma so, descartando
   product_id repetidos (fica a primeira ocorrencia)
   Tbm apaga os arquivos de entrada */
static int intercalar(NomeArquivo *entradas, int n, const char *saida)
{
    FILE *via[MAX_VIAS];
    Produto atual[MAX_VIAS];
    int tem[MAX_VIAS];
    FILE *out = fopen(saida, "wb");
    long long ultima = 0;
    int gravou = 0, i;

    if (!out)
        return 0;
    for (i = 0; i < n; i++) {
        via[i] = fopen(entradas[i], "rb");
        tem[i] = via[i] && fread(&atual[i], sizeof(Produto), 1, via[i]) == 1;
    }

    for (;;) {
        int menor = -1;

        for (i = 0; i < n; i++)
            if (tem[i] && (menor < 0 || atual[i].product_id < atual[menor].product_id))
                menor = i;
        if (menor < 0)
            break;

        if (!gravou || atual[menor].product_id != ultima) {
            fwrite(&atual[menor], sizeof(Produto), 1, out);
            ultima = atual[menor].product_id;
            gravou = 1;
        }
        tem[menor] = fread(&atual[menor], sizeof(Produto), 1, via[menor]) == 1;
    }

    for (i = 0; i < n; i++) {
        if (via[i])
            fclose(via[i]);
        remove(entradas[i]);
    }
    fclose(out);
    return 1;
}

// Intercala as runs em passadas de ate MAX_VIAS arquivos ate sobrar uma
static int ordenacao_externa(ListaRuns *l, const char *destino)
{
    while (l->qtd > 1) {
        ListaRuns prox = {NULL, 0, 0, l->proximo_id};
        int i;

        for (i = 0; i < l->qtd; i += MAX_VIAS) {
            int n = l->qtd - i < MAX_VIAS ? l->qtd - i : MAX_VIAS;
            const char *saida = nova_run(&prox);

            if (!saida || !intercalar(&l->nomes[i], n, saida)) {
                free(prox.nomes);
                return 0;
            }
        }
        printf("  intercalacao: %d arquivos -> %d\n", l->qtd, prox.qtd);
        free(l->nomes);
        *l = prox;
    }

    remove(destino);
    if (l->qtd == 0) {
        FILE *f = fopen(destino, "wb");   // nenhum produto
        if (f)
            fclose(f);
        return f != NULL;
    }
    return rename(l->nomes[0], destino) == 0;
}

// Conta as linhas de dados do CSV
static long contar_linhas(const char *caminho)
{
    FILE *f = fopen(caminho, "rb");
    static char bloco[1 << 16];
    int n, i;
    long linhas = 0;

    if (!f)
        return 0;
    while ((n = (int)fread(bloco, 1, sizeof bloco, f)) > 0)
        for (i = 0; i < n; i++)
            if (bloco[i] == '\n')
                linhas++;
    fclose(f);
    return linhas > 0 ? linhas - 1 : 0;
}

int importar_csv(const char *caminho_csv, long limite,
                 long *n_acessos, long *n_produtos)
{
    FILE *csv = fopen(caminho_csv, "r");
    FILE *acessos;
    ProdutoLido *buf;
    int n_buf = 0;
    ListaRuns runs = {NULL, 0, 0, 0};
    char linha[TAM_LINHA];
    long id = 0;
    long total, passo = 1, n_linha;
    int ok = 1;

    if (!csv) {
        printf("Nao foi possivel abrir %s\n", caminho_csv);
        return 0;
    }

    if (limite > 0) {
        printf("  contando as linhas do CSV...\n");
        total = contar_linhas(caminho_csv);
        if (total > limite)
            passo = total / limite;
        printf("  %ld linhas no CSV: importando 1 a cada %ld\n", total, passo);
    }
    _mkdir(DIR_DADOS);   // cria a pasta dados/; se ja existe, nao faz nada
    acessos = fopen(ARQ_ACESSOS, "wb");
    buf = malloc(TAM_RUN * sizeof *buf);
    if (!acessos || !buf) {
        printf("Nao foi possivel criar os arquivos em %s\n", DIR_DADOS);
        fclose(csv);
        if (acessos)
            fclose(acessos);
        free(buf);
        return 0;
    }

    fgets(linha, sizeof linha, csv);   // cabecalho

    for (n_linha = 0; (limite <= 0 || id < limite)
                      && fgets(linha, sizeof linha, csv); n_linha++) {
        Acesso a;

        if (n_linha % passo != 0)
            continue;
        if (!csv_converter_linha(linha, id + 1, &a, &buf[n_buf].p))
            continue;
        buf[n_buf].ordem = n_buf;
        id++;
        fwrite(&a, sizeof a, 1, acessos);

        if (++n_buf == TAM_RUN) {
            ok = ok && gravar_run(&runs, buf, n_buf);
            n_buf = 0;
        }
        if (id % 100000 == 0)
            printf("  %ld linhas importadas\n", id);
    }
    if (n_buf > 0)
        ok = ok && gravar_run(&runs, buf, n_buf);

    fclose(csv);
    fclose(acessos);
    free(buf);

    printf("  ordenando produtos (%d blocos de ate %d)...\n", runs.qtd, TAM_RUN);
    ok = ok && ordenacao_externa(&runs, ARQ_PRODUTOS);
    free(runs.nomes);

    // indices e overflow antigos nao valem mais
    remove(ARQ_PRODUTOS_OVERFLOW);
    remove(ARQ_PRODUTOS_INDICE);
    remove(ARQ_ACESSOS_OVERFLOW);
    remove(ARQ_ACESSOS_INDICE);

    *n_acessos = id;
    *n_produtos = (long)(arq_tamanho_bytes(ARQ_PRODUTOS) / sizeof(Produto));
    return ok;
}
