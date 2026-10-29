#include <stdint.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <direct.h>
#else
#include <sys/stat.h>
#endif

/* Gera os arquivos de dados a partir do CSV do dataset (inserção dos
   dados iniciais):

   - acessos.dat: um registro por linha importada, com id sequencial. O
     CSV já está em ordem cronológica, então os ids saem ordenados;
   - produtos.dat: um registro por product_id distinto, ordenado por
     ordenação externa (blocos ordenados em memória + intercalação),
     para não carregar o arquivo inteiro na RAM.

   limite: quantidade de linhas a importar (0 = todas). Com limite, a
   amostra é sistemática: 1 linha a cada k, com k = total / limite, para
   cobrir todo o período do CSV e não só as primeiras horas.
   Os índices e áreas de overflow antigos são apagados. */
int importar_csv(const char *caminho_csv, int64_t limite,
                 int64_t *n_acessos, int64_t *n_produtos);

#ifdef _WIN32
#define criar_pasta(p) _mkdir(p)
#else
#define criar_pasta(p) mkdir(p, 0755)
#endif

#define TAM_LINHA 1024
#define TAM_NOME  64

typedef char NomeArquivo[TAM_NOME];

typedef struct {
    NomeArquivo *nomes;
    int          qtd;
    int          capacidade;
    int          proximo_id;
} ListaRuns;

static int cmp_produto(const void *a, const void *b)
{
    uint64_t x = ((const Produto *)a)->product_id;
    uint64_t y = ((const Produto *)b)->product_id;

    return (x > y) - (x < y);
}

static const char *nova_run(ListaRuns *l)
{
    if (l->qtd == l->capacidade) {
        int nova = l->capacidade ? l->capacidade * 2 : 16;
        NomeArquivo *v = realloc(l->nomes, (size_t)nova * sizeof *v);

        if (!v)
            return NULL;
        l->nomes = v;
        l->capacidade = nova;
    }
    snprintf(l->nomes[l->qtd], TAM_NOME, DIR_DADOS "run_%04d.tmp", l->proximo_id++);
    return l->nomes[l->qtd++];
}

/* Ordena o bloco em memória e grava sem product_id repetido. */
static int gravar_run(ListaRuns *l, Produto *buf, size_t n)
{
    const char *nome = nova_run(l);
    FILE *f;
    size_t i;

    if (!nome || !(f = fopen(nome, "wb")))
        return 0;
    qsort(buf, n, sizeof *buf, cmp_produto);
    for (i = 0; i < n; i++)
        if (i == 0 || buf[i].product_id != buf[i - 1].product_id)
            fwrite(&buf[i], sizeof *buf, 1, f);
    fclose(f);
    return 1;
}

/* Intercala até MAX_VIAS runs ordenadas em uma só, descartando
   product_id repetidos (fica a primeira ocorrência). Apaga as entradas. */
static int intercalar(NomeArquivo *entradas, int n, const char *saida)
{
    FILE *via[MAX_VIAS];
    Produto atual[MAX_VIAS];
    int tem[MAX_VIAS];
    FILE *out = fopen(saida, "wb");
    uint64_t ultima = 0;
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

/* Intercala as runs em passadas de até MAX_VIAS arquivos até sobrar uma. */
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
        FILE *f = fopen(destino, "wb");   /* nenhum produto */
        if (f)
            fclose(f);
        return f != NULL;
    }
    return rename(l->nomes[0], destino) == 0;
}

/* Conta as linhas de dados do CSV (sem o cabeçalho), lendo em blocos. */
static int64_t contar_linhas(const char *caminho)
{
    FILE *f = fopen(caminho, "rb");
    static char bloco[1 << 16];
    size_t n, i;
    int64_t linhas = 0;

    if (!f)
        return 0;
    while ((n = fread(bloco, 1, sizeof bloco, f)) > 0)
        for (i = 0; i < n; i++)
            if (bloco[i] == '\n')
                linhas++;
    fclose(f);
    return linhas > 0 ? linhas - 1 : 0;
}

int importar_csv(const char *caminho_csv, int64_t limite,
                 int64_t *n_acessos, int64_t *n_produtos)
{
    FILE *csv = fopen(caminho_csv, "r");
    FILE *acessos;
    Produto *buf;
    size_t n_buf = 0;
    ListaRuns runs = {NULL, 0, 0, 0};
    char linha[TAM_LINHA];
    uint64_t id = 0;
    int64_t total, passo = 1, n_linha;
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
        printf("  %" PRId64 " linhas no CSV: importando 1 a cada %" PRId64 "\n",
               total, passo);
    }
    criar_pasta(DIR_DADOS);   /* falha sem problema se já existe */
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

    fgets(linha, sizeof linha, csv);   /* cabeçalho */

    for (n_linha = 0; (limite <= 0 || (int64_t)id < limite)
                      && fgets(linha, sizeof linha, csv); n_linha++) {
        Acesso a;

        if (n_linha % passo != 0)
            continue;
        if (!csv_converter_linha(linha, id + 1, &a, &buf[n_buf]))
            continue;
        id++;
        fwrite(&a, sizeof a, 1, acessos);

        if (++n_buf == TAM_RUN) {
            ok = ok && gravar_run(&runs, buf, n_buf);
            n_buf = 0;
        }
        if (id % 100000 == 0)
            printf("  %" PRIu64 " linhas importadas\n", id);
    }
    if (n_buf > 0)
        ok = ok && gravar_run(&runs, buf, n_buf);

    fclose(csv);
    fclose(acessos);
    free(buf);

    printf("  ordenando produtos (%d blocos de ate %d)...\n", runs.qtd, TAM_RUN);
    ok = ok && ordenacao_externa(&runs, ARQ_PRODUTOS);
    free(runs.nomes);

    /* índices e overflow antigos não valem mais */
    remove(ARQ_PRODUTOS_OVERFLOW);
    remove(ARQ_PRODUTOS_INDICE);
    remove(ARQ_ACESSOS_OVERFLOW);
    remove(ARQ_ACESSOS_INDICE);

    *n_acessos = (int64_t)id;
    *n_produtos = arq_tamanho_bytes(ARQ_PRODUTOS) / (int64_t)sizeof(Produto);
    return ok;
}
