#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef enum { AREA_PRINCIPAL, AREA_OVERFLOW } Area;

typedef struct {
    const char *nome;
    const char *arq_dados;
    const char *arq_overflow;
    const char *arq_indice;
    int         tam;
    int         bloco1, bloco2;
    long        limite_overflow;
    long        limite_removidos;
    void      (*cabecalho)(void);
    void      (*imprimir)(const void *reg);

    FILE  *dados;
    FILE  *overflow;
    long   n_principal;
    long   n_overflow;
    Indice indice;
    long   leituras;     // tudo que fou lido na ultima leitura
} Tabela;

typedef struct {
    Tabela *t;
    long    ip, io;
    char   *rp, *ro;
    int     tem_p, tem_o;
} Iterador;

void tabela_configurar(Tabela *t, const char *nome, const char *arq_dados,
                       const char *arq_overflow, const char *arq_indice, int tam,
                       int bloco1, int bloco2,
                       void (*cabecalho)(void), void (*imprimir)(const void *));

// Abre os arquivos e carrega o indice
int  tabela_abrir(Tabela *t);
// Grava os indices e fecha os arquivos
void tabela_fechar(Tabela *t);

// Pesquisa binaria direto no arquivo de dados (sem indice)
int tabela_busca_binaria(Tabela *t, long long chave, void *reg);
// Pesquisa pelo indice parcial (nivel 2 -> nivel 1 -> bloco de dados)
int tabela_busca_indice(Tabela *t, long long chave, void *reg);

// Retorna 1 se inseriu, 0 se a chave ja existe, -1 em erro
int tabela_inserir(Tabela *t, void *reg);
// Retorna 1 se removeu, 0 se nao encontrou
int tabela_remover(Tabela *t, long long chave);

int tabela_reorganizar(Tabela *t);

long long tabela_maior_chave(Tabela *t);

// Percorre os registros ativos em ordem de chave (principais e de overflow)
int  iter_iniciar(Iterador *it, Tabela *t);
int  iter_proximo(Iterador *it, void *reg);
void iter_fim(Iterador *it);

void tabela_mostrar(Tabela *t, long inicio, long qtd);
void tabela_mostrar_overflow(Tabela *t);
void tabela_info(Tabela *t);

typedef struct {
    Area area;
    long pos;
} Local;

static long long chave_de(const void *reg)
{
    long long k;

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
                       const char *arq_overflow, const char *arq_indice, int tam,
                       int bloco1, int bloco2,
                       void (*cabecalho)(void), void (*imprimir)(const void *))
{
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
    t->dados = NULL;
    t->overflow = NULL;
    t->n_principal = 0;
    t->n_overflow = 0;
    t->leituras = 0;
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

void tabela_fechar(Tabela *t){
    indice_salvar(&t->indice);
    indice_liberar(&t->indice);
    if (t->dados)
        fclose(t->dados);
    if (t->overflow)
        fclose(t->overflow);
    t->dados = t->overflow = NULL;
}

// Pesquisa binaria, retorna a posicao do registro com a chave ou -1 
static long busca_binaria(Tabela *t, FILE *f, long ini, long fim,
                          long long chave, void *reg)
{
    while (ini < fim) {
        long meio = ini + (fim - ini) / 2;
        long long k;

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

// localiza a chave no arq principal, se nao encontrar vai pro overflow, tbm coloquei pra achar nos removidos 
static int localizar(Tabela *t, long long chave, void *reg, int usar_indice, Local *loc)
{
    long ini = 0, fim = t->n_principal;

    t->leituras = 0;
    if (usar_indice && !indice_bloco(&t->indice, chave, &ini, &fim, &t->leituras))
        ini = fim = 0;   // nao esta na area principal

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

int tabela_busca_binaria(Tabela *t, long long chave, void *reg)
{
    Local loc;

    return localizar(t, chave, reg, 0, &loc) && ativo(t, reg);
}

int tabela_busca_indice(Tabela *t, long long chave, void *reg)
{
    Local loc;

    return localizar(t, chave, reg, 1, &loc) && ativo(t, reg);
}

int tabela_inserir(Tabela *t, void *reg)
{
    char *aux = malloc(t->tam);
    long long chave = chave_de(reg);
    long pos;
    Local loc;

    if (!aux)
        return -1;
    marcar(t, reg, STATUS_ATIVO);

    if (localizar(t, chave, aux, 1, &loc)) {
        if (ativo(t, aux)) {
            free(aux);
            return 0;
        }
        arq_gravar(arquivo_da_area(t, loc.area), loc.pos, reg, t->tam);
        if (t->indice.cab.removidos > 0)
            t->indice.cab.removidos--;
        indice_salvar(&t->indice);   // grava o contador na hora
        free(aux);
        return 1;
    }

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
        printf("Area de overflow de %s cheia (%ld registros): reorganizando.\n",
               t->nome, t->n_overflow);
        tabela_reorganizar(t);
    }
    return 1;
}

int tabela_remover(Tabela *t, long long chave)
{
    char *aux = malloc(t->tam);
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
    /* grava o contador no cabecalho do indice na hora, junto com o '*' no
       arquivo de dados: se o programa for fechado sem o "0 - Sair", nao perde */
    indice_salvar(&t->indice);
    if (t->indice.cab.removidos >= t->limite_removidos) {
        printf("%lld remocoes pendentes em %s: reorganizando.\n",
               t->indice.cab.removidos, t->nome);
        tabela_reorganizar(t);
    }
    return 1;
}

int tabela_reorganizar(Tabela *t)
{
    char temp[512], backup[512];
    char *reg = malloc(t->tam);
    FILE *novo;
    Iterador it;
    long gravados = 0;
    int ok = 1;

    snprintf(temp, sizeof temp, "%s.tmp", t->arq_dados);
    snprintf(backup, sizeof backup, "%s.bak", t->arq_dados);
    novo = fopen(temp, "wb");
    if (!reg || !novo || !iter_iniciar(&it, t)) {
        free(reg);
        if (novo)
            fclose(novo);
        return 0;
    }

    
    //descarta os registros removidos 
    while (ok && iter_proximo(&it, reg)) {
        ok = fwrite(reg, t->tam, 1, novo) == 1;
        gravados++;
    }
    iter_fim(&it);
    if (fclose(novo) != 0)
        ok = 0;
    free(reg);

    if (!ok) {
        remove(temp);
        printf("Erro ao gravar %s: reorganizacao cancelada.\n", temp);
        return 0;
    }

    fclose(t->dados);
    fclose(t->overflow);
    remove(backup);
    if (rename(t->arq_dados, backup) != 0) {
        ok = 0;
    } else if (rename(temp, t->arq_dados) != 0) {
        rename(backup, t->arq_dados);   
        ok = 0;
    }
    if (!ok) {
        remove(temp);
        printf("Erro ao substituir %s: reorganizacao cancelada, "
               "arquivos originais mantidos.\n", t->arq_dados);
        t->dados = fopen(t->arq_dados, "r+b");
        t->overflow = fopen(t->arq_overflow, "r+b");
        return 0;
    }
    remove(backup);

    t->dados = fopen(t->arq_dados, "r+b");
    t->overflow = fopen(t->arq_overflow, "w+b");   // esvazia o overflow
    if (!t->dados || !t->overflow)
        return 0;
    t->n_principal = gravados;
    t->n_overflow = 0;

    if (!reconstruir_indice(t))
        return 0;
    indice_salvar(&t->indice);
    printf("%s reorganizado: %ld registros, indice reconstruido.\n",
           t->nome, gravados);
    return 1;
}

long long tabela_maior_chave(Tabela *t)
{
    char *reg = malloc(t->tam);
    long long maior = 0, k;

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

// ------------------------------------------------------------------
// Iterador: intercala as duas areas lendo um registro de cada vez
// ------------------------------------------------------------------

int iter_iniciar(Iterador *it, Tabela *t)
{
    it->t = t;
    it->ip = 0;
    it->io = 0;
    it->tem_p = 0;
    it->tem_o = 0;
    it->rp = malloc(t->tam);
    it->ro = malloc(t->tam);
    if (!it->rp || !it->ro) {
        iter_fim(it);
        return 0;
    }
    // leitura sequencial: posiciona uma vez e depois so fread
    arq_seek(t->dados, 0);
    arq_seek(t->overflow, 0);
    return 1;
}

int iter_proximo(Iterador *it, void *reg)
{
    Tabela *t = it->t;

    for (;;) {
        char *escolhido;

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

// ------------------------------------------------------------------
// Exibicao
// ------------------------------------------------------------------

void tabela_mostrar(Tabela *t, long inicio, long qtd)
{
    char *reg = malloc(t->tam);
    Iterador it;
    long i = 0, mostrados = 0;

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
    printf("(%ld registros mostrados)\n", mostrados);
}

void tabela_mostrar_overflow(Tabela *t)
{
    char *reg = malloc(t->tam);
    long i;

    if (!reg)
        return;
    printf("Area de overflow de %s: %ld registros ('*' = removido)\n",
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
    printf("Arquivo de dados ........: %s (%lld bytes)\n",
           t->arq_dados, arq_tamanho_bytes(t->arq_dados));
    printf("Tamanho do registro .....: %d bytes\n", t->tam);
    printf("Area principal ..........: %ld registros\n", t->n_principal);
    printf("Area de overflow ........: %ld registros (limite %ld)\n",
           t->n_overflow, t->limite_overflow);
    printf("Remocoes pendentes ......: %lld (limite %ld)\n",
           c->removidos, t->limite_removidos);
    printf("Arquivo de indice .......: %s (%lld bytes)\n",
           t->arq_indice, arq_tamanho_bytes(t->arq_indice));
    printf("Nivel 1 .................: %lld entradas (1 a cada %d registros)\n",
           c->n1, c->bloco1);
    printf("Nivel 2 .................: %lld entradas (1 a cada %d entradas do nivel 1)\n",
           c->n2, c->bloco2);
}
