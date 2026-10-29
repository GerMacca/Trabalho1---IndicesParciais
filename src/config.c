/* ------------------------------------------------------------------ */
/* Caminhos (relativos à pasta raiz do projeto, de onde o programa     */
/* deve ser executado)                                                 */
/* ------------------------------------------------------------------ */
#define DIR_DADOS "dados/"

#define ARQ_PRODUTOS          DIR_DADOS "produtos.dat"
#define ARQ_PRODUTOS_OVERFLOW DIR_DADOS "produtos_overflow.dat"
#define ARQ_PRODUTOS_INDICE   DIR_DADOS "produtos.idx"

#define ARQ_ACESSOS           DIR_DADOS "acessos.dat"
#define ARQ_ACESSOS_OVERFLOW  DIR_DADOS "acessos_overflow.dat"
#define ARQ_ACESSOS_INDICE    DIR_DADOS "acessos.idx"

/* ------------------------------------------------------------------ */
/* Importação                                                          */
/* ------------------------------------------------------------------ */
#define LIMITE_LINHAS_PADRAO 200000  /* linhas do CSV lidas por padrão  */
#define TAM_RUN              50000   /* produtos por bloco da ordenação externa */
#define MAX_VIAS             16      /* arquivos intercalados por vez   */

/* ------------------------------------------------------------------ */
/* Índice parcial (2 níveis)                                           */
/*   nível 1: 1 entrada a cada BLOCO1 registros do arquivo de dados    */
/*   nível 2: 1 entrada a cada BLOCO2 entradas do nível 1              */
/* ------------------------------------------------------------------ */
#define PRODUTOS_BLOCO1 64
#define PRODUTOS_BLOCO2 32
#define ACESSOS_BLOCO1  128
#define ACESSOS_BLOCO2  64

/* ------------------------------------------------------------------ */
/* Critérios de reorganização (reconstrução do arquivo e do índice)    */
/* ------------------------------------------------------------------ */
#define LIMITE_OVERFLOW  100  /* registros na área de overflow          */
#define LIMITE_REMOVIDOS 100  /* remoções lógicas pendentes             */
