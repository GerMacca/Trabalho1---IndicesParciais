#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>

/* Arquivo de dados com organização sequencial-indexada:

   - área principal (arquivo .dat): registros de tamanho fixo ordenados
     pela chave, sem buracos; é a única área coberta pelo índice parcial;
   - área de overflow (arquivo _overflow.dat): recebe as inserções,
     mantida ordenada pela chave e limitada a limite_overflow registros;
   - remoção lógica: o registro é marcado com '*' e fica no arquivo;
   - reorganização: intercala a área principal com o overflow,
     descartando os removidos, e reconstrói o índice. Acontece quando o
     overflow enche ou quando as remoções pendentes chegam ao limite.

   Os registros seguem a convenção de registros.c (chave nos 8 primeiros
   bytes, status no penúltimo byte, '\n' no último). */

typedef enum { AREA_PRINCIPAL, AREA_OVERFLOW } Area;

typedef struct {
    const char *nome;
    const char *arq_dados;
    const char *arq_overflow;
    const char *arq_indice;
    size_t      tam;
    uint32_t    bloco1, bloco2;
    int64_t     limite_overflow;
    int64_t     limite_removidos;
    void      (*cabecalho)(void);
    void      (*imprimir)(const void *reg);

    FILE   *dados;
    FILE   *overflow;
    int64_t n_principal;
    int64_t n_overflow;
    Indice  indice;
    long    leituras;     /* registros lidos na última pesquisa */
} Tabela;

typedef struct {
    Tabela        *t;
    int64_t        ip, io;
    unsigned char *rp, *ro;
    int            tem_p, tem_o;
} Iterador;

void tabela_configurar(Tabela *t, const char *nome, const char *arq_dados,
                       const char *arq_overflow, const char *arq_indice, size_t tam,
                       uint32_t bloco1, uint32_t bloco2,
                       void (*cabecalho)(void), void (*imprimir)(const void *));

/* Abre os arquivos e carrega o índice (ou o reconstrói). */
int  tabela_abrir(Tabela *t);
/* Grava o índice e fecha os arquivos. */
void tabela_fechar(Tabela *t);

/* Pesquisa binária direto no arquivo de dados (sem índice). */
int tabela_busca_binaria(Tabela *t, uint64_t chave, void *reg);
/* Pesquisa pelo índice parcial (nível 2 -> nível 1 -> bloco de dados). */
int tabela_busca_indice(Tabela *t, uint64_t chave, void *reg);

/* Retorna 1 se inseriu, 0 se a chave já existe, -1 em erro. */
int tabela_inserir(Tabela *t, void *reg);
/* Retorna 1 se removeu, 0 se não encontrou. */
int tabela_remover(Tabela *t, uint64_t chave);
int tabela_reorganizar(Tabela *t);

uint64_t tabela_maior_chave(Tabela *t);

/* Percorre os registros ativos em ordem de chave (principal + overflow). */
int  iter_iniciar(Iterador *it, Tabela *t);
int  iter_proximo(Iterador *it, void *reg);
void iter_fim(Iterador *it);

void tabela_mostrar(Tabela *t, int64_t inicio, int64_t qtd);
void tabela_mostrar_overflow(Tabela *t);
void tabela_info(Tabela *t);

typedef struct {
    Area    area;
    int64_t pos;
} Local;

static uint64_t chave_de(const void *reg)
{
    uint64_t k;

    memcpy(&k, reg, sizeof k);
    return k;
}

static int ativo(const Tabela *t, const void *reg)
{
    return ((const char *)reg)[t->tam - 2] != STATUS_REMOVIDO;
}

static void marcar(const Tabela *t, void *reg, char status)
{
    ((char *)reg)[t->tam - 2] = status;
    ((char *)reg)[t->tam - 1] = '\n';
}

static FILE *arquivo_da_area(Tabela *t, Area area)
{
    return area == AREA_PRINCIPAL ? t->dados : t->overflow;
}

void tabela_configurar(Tabela *t, const char *nome, const char *arq_dados,
                       const char *arq_overflow, const char *arq_indice, size_t tam,
                       uint32_t bloco1, uint32_t bloco2,
                       void (*cabecalho)(void), void (*imprimir)(const void *))
{
    memset(t, 0, sizeof *t);
    t->nome = nome;
    t->arq_dados = arq_dados;
    t->arq_overflow = arq_overflow;
    t->arq_indice = arq_indice;
    t->tam = tam;
    t->bloco1 = bloco1;
    t->bloco2 = bloco2;
    t->limite_overflow = LIMITE_OVERFLOW;
    t->limite_removidos = LIMITE_REMOVIDOS;
    t->cabecalho = cabecalho;
    t->imprimir = imprimir;
    indice_iniciar(&t->indice);
}

static int reconstruir_indice(Tabela *t)
{
    return indice_construir(&t->indice, t->arq_indice, t->dados, t->tam,
                            t->bloco1, t->bloco2);
}

int tabela_abrir(Tabela *t)
{
    t->dados = fopen(t->arq_dados, "r+b");
    if (!t->dados)
        return 0;

    t->overflow = fopen(t->arq_overflow, "r+b");
    if (!t->overflow)
        t->overflow = fopen(t->arq_overflow, "w+b");
    if (!t->overflow) {
        fclose(t->dados);
        t->dados = NULL;
        return 0;
    }

    t->n_principal = arq_qtd_registros(t->dados, t->tam);
    t->n_overflow = arq_qtd_registros(t->overflow, t->tam);

    if (!indice_carregar(&t->indice, t->arq_indice, t->n_principal, t->bloco1, t->bloco2)) {
        printf("Indice de %s ausente ou desatualizado: reconstruindo...\n", t->nome);
        if (!reconstruir_indice(t))
            return 0;
    }
    return 1;
}

void tabela_fechar(Tabela *t)
{
    indice_salvar(&t->indice);
    indice_liberar(&t->indice);
    if (t->dados)
        fclose(t->dados);
    if (t->overflow)
        fclose(t->overflow);
    t->dados = t->overflow = NULL;
}

/* Pesquisa binária nos registros [ini, fim) de um arquivo ordenado.
   Retorna a posição do registro com a chave (ativo ou não) ou -1. */
static int64_t busca_binaria(Tabela *t, FILE *f, int64_t ini, int64_t fim,
                             uint64_t chave, void *reg)
{
    while (ini < fim) {
        int64_t meio = ini + (fim - ini) / 2;
        uint64_t k;

        if (!arq_ler(f, meio, reg, t->tam))
            return -1;
        t->leituras++;
        k = chave_de(reg);
        if (k == chave)
            return meio;
        if (k < chave)
            ini = meio + 1;
        else
            fim = meio;
    }
    return -1;
}

/* Localiza a chave na área principal (com ou sem índice) e, se não
   achar, no overflow. Encontra também registros removidos. */
static int localizar(Tabela *t, uint64_t chave, void *reg, int usar_indice, Local *loc)
{
    int64_t ini = 0, fim = t->n_principal;

    t->leituras = 0;
    if (usar_indice && !indice_bloco(&t->indice, chave, &ini, &fim, &t->leituras))
        ini = fim = 0;   /* chave menor que todas: não está na área principal */

    loc->pos = busca_binaria(t, t->dados, ini, fim, chave, reg);
    if (loc->pos >= 0) {
        loc->area = AREA_PRINCIPAL;
        return 1;
    }

    loc->pos = busca_binaria(t, t->overflow, 0, t->n_overflow, chave, reg);
    if (loc->pos >= 0) {
        loc->area = AREA_OVERFLOW;
        return 1;
    }
    return 0;
}

int tabela_busca_binaria(Tabela *t, uint64_t chave, void *reg)
{
    Local loc;

    return localizar(t, chave, reg, 0, &loc) && ativo(t, reg);
}

int tabela_busca_indice(Tabela *t, uint64_t chave, void *reg)
{
    Local loc;

    return localizar(t, chave, reg, 1, &loc) && ativo(t, reg);
}

int tabela_inserir(Tabela *t, void *reg)
{
    unsigned char *aux = malloc(t->tam);
    uint64_t chave = chave_de(reg);
    int64_t pos;
    Local loc;

    if (!aux)
        return -1;
    marcar(t, reg, STATUS_ATIVO);

    if (localizar(t, chave, aux, 1, &loc)) {
        if (ativo(t, aux)) {
            free(aux);
            return 0;
        }
        /* chave de um registro removido: reaproveita o mesmo espaço,
           a ordem do arquivo continua correta */
        arq_gravar(arquivo_da_area(t, loc.area), loc.pos, reg, t->tam);
        if (t->indice.cab.removidos > 0)
            t->indice.cab.removidos--;
        free(aux);
        return 1;
    }

    /* insere no overflow mantendo a ordem: desloca os maiores uma posição */
    pos = t->n_overflow;
    while (pos > 0) {
        arq_ler(t->overflow, pos - 1, aux, t->tam);
        if (chave_de(aux) < chave)
            break;
        arq_gravar(t->overflow, pos, aux, t->tam);
        pos--;
    }
    arq_gravar(t->overflow, pos, reg, t->tam);
    t->n_overflow++;
    free(aux);

    if (t->n_overflow >= t->limite_overflow) {
        printf("Area de overflow de %s cheia (%" PRId64 " registros): reorganizando.\n",
               t->nome, t->n_overflow);
        tabela_reorganizar(t);
    }
    return 1;
}

int tabela_remover(Tabela *t, uint64_t chave)
{
    unsigned char *aux = malloc(t->tam);
    Local loc;

    if (!aux)
        return 0;
    if (!localizar(t, chave, aux, 1, &loc) || !ativo(t, aux)) {
        free(aux);
        return 0;
    }

    marcar(t, aux, STATUS_REMOVIDO);
    arq_gravar(arquivo_da_area(t, loc.area), loc.pos, aux, t->tam);
    free(aux);

    t->indice.cab.removidos++;
    if (t->indice.cab.removidos >= t->limite_removidos) {
        printf("%" PRId64 " remocoes pendentes em %s: reorganizando.\n",
               t->indice.cab.removidos, t->nome);
        tabela_reorganizar(t);
    }
    return 1;
}

int tabela_reorganizar(Tabela *t)
{
    char temp[512];
    unsigned char *reg = malloc(t->tam);
    FILE *novo;
    Iterador it;
    int64_t gravados = 0;

    snprintf(temp, sizeof temp, "%s.tmp", t->arq_dados);
    novo = fopen(temp, "wb");
    if (!reg || !novo || !iter_iniciar(&it, t)) {
        free(reg);
        if (novo)
            fclose(novo);
        return 0;
    }

    /* intercalação da área principal com o overflow; o iterador já
       descarta os registros removidos */
    while (iter_proximo(&it, reg)) {
        fwrite(reg, t->tam, 1, novo);
        gravados++;
    }
    iter_fim(&it);
    fclose(novo);
    free(reg);

    fclose(t->dados);
    fclose(t->overflow);
    remove(t->arq_dados);
    rename(temp, t->arq_dados);

    t->dados = fopen(t->arq_dados, "r+b");
    t->overflow = fopen(t->arq_overflow, "w+b");   /* esvazia o overflow */
    if (!t->dados || !t->overflow)
        return 0;
    t->n_principal = gravados;
    t->n_overflow = 0;

    if (!reconstruir_indice(t))
        return 0;
    indice_salvar(&t->indice);
    printf("%s reorganizado: %" PRId64 " registros, indice reconstruido.\n",
           t->nome, gravados);
    return 1;
}

uint64_t tabela_maior_chave(Tabela *t)
{
    unsigned char *reg = malloc(t->tam);
    uint64_t maior = 0, k;

    if (!reg)
        return 0;
    if (t->n_principal > 0 && arq_ler(t->dados, t->n_principal - 1, reg, t->tam))
        maior = chave_de(reg);
    if (t->n_overflow > 0 && arq_ler(t->overflow, t->n_overflow - 1, reg, t->tam)) {
        k = chave_de(reg);
        if (k > maior)
            maior = k;
    }
    free(reg);
    return maior;
}

/* ------------------------------------------------------------------ */
/* Iterador: intercala as duas áreas lendo um registro de cada vez     */
/* ------------------------------------------------------------------ */

int iter_iniciar(Iterador *it, Tabela *t)
{
    memset(it, 0, sizeof *it);
    it->t = t;
    it->rp = malloc(t->tam);
    it->ro = malloc(t->tam);
    if (!it->rp || !it->ro) {
        iter_fim(it);
        return 0;
    }
    /* leitura sequencial: posiciona uma vez e depois só fread */
    arq_seek(t->dados, 0);
    arq_seek(t->overflow, 0);
    return 1;
}

int iter_proximo(Iterador *it, void *reg)
{
    Tabela *t = it->t;

    for (;;) {
        unsigned char *escolhido;

        if (!it->tem_p && it->ip < t->n_principal) {
            it->tem_p = fread(it->rp, t->tam, 1, t->dados) == 1;
            it->ip++;
        }
        if (!it->tem_o && it->io < t->n_overflow) {
            it->tem_o = fread(it->ro, t->tam, 1, t->overflow) == 1;
            it->io++;
        }
        if (!it->tem_p && !it->tem_o)
            return 0;

        if (it->tem_p && (!it->tem_o || chave_de(it->rp) <= chave_de(it->ro))) {
            escolhido = it->rp;
            it->tem_p = 0;
        } else {
            escolhido = it->ro;
            it->tem_o = 0;
        }
        if (ativo(t, escolhido)) {
            memcpy(reg, escolhido, t->tam);
            return 1;
        }
    }
}

void iter_fim(Iterador *it)
{
    free(it->rp);
    free(it->ro);
    it->rp = it->ro = NULL;
}

/* ------------------------------------------------------------------ */
/* Exibição                                                            */
/* ------------------------------------------------------------------ */

void tabela_mostrar(Tabela *t, int64_t inicio, int64_t qtd)
{
    unsigned char *reg = malloc(t->tam);
    Iterador it;
    int64_t i = 0, mostrados = 0;

    if (!reg || !iter_iniciar(&it, t)) {
        free(reg);
        return;
    }
    t->cabecalho();
    while ((qtd <= 0 || mostrados < qtd) && iter_proximo(&it, reg)) {
        if (i++ < inicio)
            continue;
        t->imprimir(reg);
        mostrados++;
    }
    iter_fim(&it);
    free(reg);
    printf("(%" PRId64 " registros mostrados)\n", mostrados);
}

void tabela_mostrar_overflow(Tabela *t)
{
    unsigned char *reg = malloc(t->tam);
    int64_t i;

    if (!reg)
        return;
    printf("Area de overflow de %s: %" PRId64 " registros ('*' = removido)\n",
           t->nome, t->n_overflow);
    t->cabecalho();
    for (i = 0; i < t->n_overflow; i++)
        if (arq_ler(t->overflow, i, reg, t->tam))
            t->imprimir(reg);
    free(reg);
}

void tabela_info(Tabela *t)
{
    const CabecalhoIndice *c = &t->indice.cab;

    printf("--- %s ---\n", t->nome);
    printf("Arquivo de dados ........: %s (%" PRId64 " bytes)\n",
           t->arq_dados, arq_tamanho_bytes(t->arq_dados));
    printf("Tamanho do registro .....: %u bytes\n", (unsigned)t->tam);
    printf("Area principal ..........: %" PRId64 " registros\n", t->n_principal);
    printf("Area de overflow ........: %" PRId64 " registros (limite %" PRId64 ")\n",
           t->n_overflow, t->limite_overflow);
    printf("Remocoes pendentes ......: %" PRId64 " (limite %" PRId64 ")\n",
           c->removidos, t->limite_removidos);
    printf("Arquivo de indice .......: %s (%" PRId64 " bytes)\n",
           t->arq_indice, arq_tamanho_bytes(t->arq_indice));
    printf("Nivel 1 .................: %" PRId64 " entradas (1 a cada %u registros)\n",
           c->n1, c->bloco1);
    printf("Nivel 2 .................: %" PRId64 " entradas (1 a cada %u entradas do nivel 1)\n",
           c->n2, c->bloco2);
}
