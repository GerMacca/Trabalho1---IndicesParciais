#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Indice parcial de 2 niveis sobre a area principal de um arquivo de
   dados ordenado pela chave

   Arquivo de indice:
     [cabecalho][entradas do nivel 1 (n1)][entradas do nivel 2 (n2)]

   Nivel 1: uma entrada a cada bloco1 registros de dados;
            chave = primeira chave do bloco, pos = numero do registro
   Nivel 2: uma entrada a cada bloco2 entradas do nivel 1;
            chave = chave da entrada, pos = numero da entrada no nivel 1
*/
typedef struct {
    long long chave;
    long long pos;
} EntradaIndice;

typedef struct {
    char      assinatura[4]; 
    int       bloco1;
    int       bloco2;
    int       reservado;
    long long n_dados;     
    long long n1;
    long long n2;
    long long removidos;  
} CabecalhoIndice;

typedef struct {
    FILE           *f;
    CabecalhoIndice cab;
    EntradaIndice  *nivel2;
} Indice;

void indice_iniciar(Indice *ind);

// Constroi o indice
int indice_construir(Indice *ind, const char *caminho, FILE *dados, int tam,
                     int bloco1, int bloco2);

int indice_carregar(Indice *ind, const char *caminho, long n_dados,
                    int bloco1, int bloco2);

int  indice_salvar(Indice *ind);
void indice_liberar(Indice *ind);

int indice_bloco(Indice *ind, long long chave, long *ini, long *fim,
                 long *leituras);

#define ASSINATURA_INDICE "IDX1"

void indice_iniciar(Indice *ind)
{
    ind->f = NULL;
    ind->nivel2 = NULL;
    strncpy(ind->cab.assinatura, "", 4);   // preenche com '\0'
    ind->cab.bloco1 = 0;
    ind->cab.bloco2 = 0;
    ind->cab.reservado = 0;
    ind->cab.n_dados = 0;
    ind->cab.n1 = 0;
    ind->cab.n2 = 0;
    ind->cab.removidos = 0;
}

void indice_liberar(Indice *ind)
{
    if (ind->f)
        fclose(ind->f);
    free(ind->nivel2);
    indice_iniciar(ind);
}

static long long offset_nivel1(long i)
{
    return (long long)sizeof(CabecalhoIndice) + (long long)i * sizeof(EntradaIndice);
}

int indice_construir(Indice *ind, const char *caminho, FILE *dados, int tam,
                     int bloco1, int bloco2)
{
    char *reg;
    long i;

    indice_liberar(ind);

    memcpy(ind->cab.assinatura, ASSINATURA_INDICE, 4);   // 4 bytes, sem '\0'
    ind->cab.bloco1 = bloco1;
    ind->cab.bloco2 = bloco2;
    ind->cab.n_dados = arq_qtd_registros(dados, tam);
    ind->cab.n1 = (ind->cab.n_dados + bloco1 - 1) / bloco1;
    ind->cab.n2 = (ind->cab.n1 + bloco2 - 1) / bloco2;
    ind->cab.removidos = 0;

    ind->f = fopen(caminho, "w+b");
    reg = malloc(tam);
    ind->nivel2 = malloc((ind->cab.n2 > 0 ? ind->cab.n2 : 1) * sizeof(EntradaIndice));
    if (!ind->f || !reg || !ind->nivel2) {
        free(reg);
        indice_liberar(ind);
        return 0;
    }

    fwrite(&ind->cab, sizeof ind->cab, 1, ind->f);

    // nivel 1 - so o primeiro registro de cada bloco e lido do arquivo de dados
    for (i = 0; i < ind->cab.n1; i++) {
        EntradaIndice e;

        e.pos = (long long)i * bloco1;
        arq_ler(dados, (long)e.pos, reg, tam);
        memcpy(&e.chave, reg, sizeof e.chave);
        fwrite(&e, sizeof e, 1, ind->f);

        if (i % bloco2 == 0) {
            ind->nivel2[i / bloco2].chave = e.chave;
            ind->nivel2[i / bloco2].pos = i;
        }
    }
    fwrite(ind->nivel2, sizeof(EntradaIndice), ind->cab.n2, ind->f);
    fflush(ind->f);

    free(reg);
    return 1;
}

int indice_carregar(Indice *ind, const char *caminho, long n_dados,
                    int bloco1, int bloco2)
{
    indice_liberar(ind);

    ind->f = fopen(caminho, "r+b");
    if (!ind->f)
        return 0;

    if (fread(&ind->cab, sizeof ind->cab, 1, ind->f) != 1
        || strncmp(ind->cab.assinatura, ASSINATURA_INDICE, 4) != 0
        || ind->cab.n_dados != n_dados
        || ind->cab.bloco1 != bloco1
        || ind->cab.bloco2 != bloco2) {
        indice_liberar(ind);
        return 0;
    }

    ind->nivel2 = malloc((ind->cab.n2 > 0 ? ind->cab.n2 : 1) * sizeof(EntradaIndice));
    if (!ind->nivel2
        || !arq_seek(ind->f, offset_nivel1((long)ind->cab.n1))
        || (long long)fread(ind->nivel2, sizeof(EntradaIndice), ind->cab.n2, ind->f)
               != ind->cab.n2) {
        indice_liberar(ind);
        return 0;
    }
    return 1;
}

int indice_salvar(Indice *ind)
{
    if (!ind->f || !arq_seek(ind->f, 0))
        return 0;
    if (fwrite(&ind->cab, sizeof ind->cab, 1, ind->f) != 1)
        return 0;
    fflush(ind->f);
    return 1;
}

int indice_bloco(Indice *ind, long long chave, long *ini, long *fim,
                 long *leituras)
{
    long lo, hi, meio, j = -1, i = -1;
    long e_ini, e_fim;
    EntradaIndice e, achada = {0, 0};

    if (!ind->f || ind->cab.n1 == 0)
        return 0;

    // nivel 2
    lo = 0;
    hi = (long)ind->cab.n2 - 1;
    while (lo <= hi) {
        meio = lo + (hi - lo) / 2;
        if (ind->nivel2[meio].chave <= chave) {
            j = meio;
            lo = meio + 1;
        } else {
            hi = meio - 1;
        }
    }
    if (j < 0)
        return 0;

    e_ini = (long)ind->nivel2[j].pos;
    e_fim = (j + 1 < ind->cab.n2) ? (long)ind->nivel2[j + 1].pos : (long)ind->cab.n1;

    // nivel 1: pesquisa binaria entre as entradas ini e fim)
    lo = e_ini;
    hi = e_fim - 1;
    while (lo <= hi) {
        meio = lo + (hi - lo) / 2;
        if (!arq_seek(ind->f, offset_nivel1(meio)) || fread(&e, sizeof e, 1, ind->f) != 1)
            return 0;
        (*leituras)++;
        if (e.chave <= chave) {
            i = meio;
            achada = e;
            lo = meio + 1;
        } else {
            hi = meio - 1;
        }
    }
    if (i < 0)
        return 0;

    *ini = (long)achada.pos;
    *fim = (long)achada.pos + ind->cab.bloco1;
    if (*fim > ind->cab.n_dados)
        *fim = (long)ind->cab.n_dados;
    return 1;
}
