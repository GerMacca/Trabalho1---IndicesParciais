// ------------------------------------------------------------------
// Caminhos (relativos a pasta raiz do projeto, de onde o programa
// deve ser executado)
// ------------------------------------------------------------------
#define DIR_DADOS "dados/"

#define ARQ_PRODUTOS          DIR_DADOS "produtos.dat"
#define ARQ_PRODUTOS_OVERFLOW DIR_DADOS "produtos_overflow.dat"
#define ARQ_PRODUTOS_INDICE   DIR_DADOS "produtos.idx"

#define ARQ_ACESSOS           DIR_DADOS "acessos.dat"
#define ARQ_ACESSOS_OVERFLOW  DIR_DADOS "acessos_overflow.dat"
#define ARQ_ACESSOS_INDICE    DIR_DADOS "acessos.idx"

// ------------------------------------------------------------------
// Importacao
// ------------------------------------------------------------------
#define LIMITE_LINHAS_PADRAO 200000  // linhas do CSV lidas por padrao
#define TAM_RUN              50000   // produtos por bloco da ordenacao externa
#define MAX_VIAS             16      // arquivos intercalados por vez

// ------------------------------------------------------------------
// Indice parcial (2 niveis)
// nivel 1: 1 entrada a cada BLOCO1 registros do arquivo de dados
// nivel 2: 1 entrada a cada BLOCO2 entradas do nivel 1
// ------------------------------------------------------------------
#define PRODUTOS_BLOCO1 64
#define PRODUTOS_BLOCO2 32
#define ACESSOS_BLOCO1  128
#define ACESSOS_BLOCO2  64

// ------------------------------------------------------------------
// Criterios de reorganizacao (reconstrucao do arquivo e do indice)
// ------------------------------------------------------------------
#define LIMITE_OVERFLOW  100  // registros na area de overflow
#define LIMITE_REMOVIDOS 100  // remocoes logicas 
