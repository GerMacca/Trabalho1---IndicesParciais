#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

/* Índice parcial de 2 níveis sobre a área principal de um arquivo de
   dados ordenado pela chave.

   Arquivo de índice:
     [cabeçalho][entradas do nível 1 (n1)][entradas do nível 2 (n2)]

   Nível 1: uma entrada a cada bloco1 registros de dados;
            chave = primeira chave do bloco, pos = número do registro.
   Nível 2: uma entrada a cada bloco2 entradas do nível 1;
            chave = chave da entrada, pos = número da entrada no nível 1.

   Ao abrir o programa o cabeçalho e o nível 2 (pequeno) são carregados
   em memória; o nível 1 é pesquisado direto no arquivo com seek. */

typedef struct {
    uint64_t chave;
    int64_t  pos;
} EntradaIndice;

typedef struct {
    uint32_t magico;
    uint32_t bloco1;
    uint32_t bloco2;
    uint32_t reservado;
    int64_t  n_dados;     /* registros na área principal quando o índice foi construído */
    int64_t  n1;
    int64_t  n2;
    int64_t  removidos;   /* remoções lógicas desde a última reorganização */
} CabecalhoIndice;

typedef struct {
    FILE           *f;
    CabecalhoIndice cab;
    EntradaIndice  *nivel2;
} Indice;

void indice_iniciar(Indice *ind);

/* Constrói o índice lendo apenas o primeiro registro de cada bloco. */
int indice_construir(Indice *ind, const char *caminho, FILE *dados, size_t tam,
                     uint32_t bloco1, uint32_t bloco2);

/* Carrega um índice existente. Retorna 0 se não existe ou não confere
   com o arquivo de dados (aí deve ser reconstruído). */
int indice_carregar(Indice *ind, const char *caminho, int64_t n_dados,
                    uint32_t bloco1, uint32_t bloco2);

int  indice_salvar(Indice *ind);
void indice_liberar(Indice *ind);

/* Encontra o bloco de dados [*ini, *fim) onde a chave pode estar.
   Retorna 0 se a chave é menor que todas as chaves indexadas. */
int indice_bloco(Indice *ind, uint64_t chave, int64_t *ini, int64_t *fim,
                 long *leituras);

#define INDICE_MAGICO 0x31584449u   /* "IDX1" */

void indice_iniciar(Indice *ind)
{
    memset(ind, 0, sizeof *ind);
}

void indice_liberar(Indice *ind)
{
    if (ind->f)
        fclose(ind->f);
    free(ind->nivel2);
    indice_iniciar(ind);
}

static int64_t offset_nivel1(int64_t i)
{
    return (int64_t)sizeof(CabecalhoIndice) + i * (int64_t)sizeof(EntradaIndice);
}

int indice_construir(Indice *ind, const char *caminho, FILE *dados, size_t tam,
                     uint32_t bloco1, uint32_t bloco2)
{
    unsigned char *reg;
    int64_t i;

    indice_liberar(ind);

    ind->cab.magico = INDICE_MAGICO;
    ind->cab.bloco1 = bloco1;
    ind->cab.bloco2 = bloco2;
    ind->cab.n_dados = arq_qtd_registros(dados, tam);
    ind->cab.n1 = (ind->cab.n_dados + bloco1 - 1) / bloco1;
    ind->cab.n2 = (ind->cab.n1 + bloco2 - 1) / bloco2;
    ind->cab.removidos = 0;

    ind->f = fopen(caminho, "w+b");
    reg = malloc(tam);
    ind->nivel2 = malloc((size_t)(ind->cab.n2 > 0 ? ind->cab.n2 : 1) * sizeof(EntradaIndice));
    if (!ind->f || !reg || !ind->nivel2) {
        free(reg);
        indice_liberar(ind);
        return 0;
    }

    fwrite(&ind->cab, sizeof ind->cab, 1, ind->f);

    /* nível 1: só o primeiro registro de cada bloco é lido do arquivo de dados */
    for (i = 0; i < ind->cab.n1; i++) {
        EntradaIndice e;

        e.pos = i * bloco1;
        arq_ler(dados, e.pos, reg, tam);
        memcpy(&e.chave, reg, sizeof e.chave);
        fwrite(&e, sizeof e, 1, ind->f);

        if (i % bloco2 == 0) {
            ind->nivel2[i / bloco2].chave = e.chave;
            ind->nivel2[i / bloco2].pos = i;
        }
    }
    fwrite(ind->nivel2, sizeof(EntradaIndice), (size_t)ind->cab.n2, ind->f);
    fflush(ind->f);

    free(reg);
    return 1;
}

int indice_carregar(Indice *ind, const char *caminho, int64_t n_dados,
                    uint32_t bloco1, uint32_t bloco2)
{
    indice_liberar(ind);

    ind->f = fopen(caminho, "r+b");
    if (!ind->f)
        return 0;

    if (fread(&ind->cab, sizeof ind->cab, 1, ind->f) != 1
        || ind->cab.magico != INDICE_MAGICO
        || ind->cab.n_dados != n_dados
        || ind->cab.bloco1 != bloco1
        || ind->cab.bloco2 != bloco2) {
        indice_liberar(ind);
        return 0;
    }

    ind->nivel2 = malloc((size_t)(ind->cab.n2 > 0 ? ind->cab.n2 : 1) * sizeof(EntradaIndice));
    if (!ind->nivel2
        || !arq_seek(ind->f, offset_nivel1(ind->cab.n1))
        || fread(ind->nivel2, sizeof(EntradaIndice), (size_t)ind->cab.n2, ind->f)
               != (size_t)ind->cab.n2) {
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

int indice_bloco(Indice *ind, uint64_t chave, int64_t *ini, int64_t *fim,
                 long *leituras)
{
    int64_t lo, hi, meio, j = -1, i = -1;
    int64_t e_ini, e_fim;
    EntradaIndice e, achada = {0, 0};

    if (!ind->f || ind->cab.n1 == 0)
        return 0;

    /* nível 2 (memória): última entrada com chave <= procurada */
    lo = 0;
    hi = ind->cab.n2 - 1;
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

    e_ini = ind->nivel2[j].pos;
    e_fim = (j + 1 < ind->cab.n2) ? ind->nivel2[j + 1].pos : ind->cab.n1;

    /* nível 1 (arquivo): pesquisa binária entre as entradas [e_ini, e_fim) */
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

    *ini = achada.pos;
    *fim = achada.pos + ind->cab.bloco1;
    if (*fim > ind->cab.n_dados)
        *fim = ind->cab.n_dados;
    return 1;
}
