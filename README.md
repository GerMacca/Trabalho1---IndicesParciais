# Trabalho 1 – Arquivos de dados com índices parciais

Algoritmos e Estruturas de Dados II. Organização **sequencial-indexada**
em C sobre o dataset
[eCommerce behavior data from multi category store](https://www.kaggle.com/datasets/mkechinov/ecommerce-behavior-data-from-multi-category-store).

## Contexto e perguntas

O dataset registra o comportamento de usuários em uma loja online com vários
departamentos: cada linha é um evento (`view`, `cart`, `remove_from_cart`,
`purchase`) de um usuário sobre um produto.

| # | Pergunta | Hipóteses |
|---|----------|-----------|
| 1 | Quais marcas são as mais compradas? | Uma marca é líder absoluta (mais de 50% das compras), **ou** há equilíbrio entre poucas marcas grandes (ex.: Samsung, Apple, Xiaomi). |
| 2 | Em que horário do dia acontecem mais compras? | As compras se concentram no horário comercial/noite do público principal, **ou** se distribuem igualmente ao longo do dia. |
| 3 | Qual a taxa de conversão do funil visualização → carrinho → compra? | Menos de 5% das visualizações viram compra; a maior parte dos produtos colocados no carrinho é comprada. |

A pergunta 1 junta os dois arquivos: percorre os acessos e, para cada compra,
busca o produto pelo **índice parcial** do arquivo de produtos.

## Arquivos de dados

Os dois arquivos são **binários**, com **registros de tamanho fixo**. Os campos
de texto são completados com espaços e cada registro termina com `'\n'`. Os
structs usam `#pragma pack(1)`, então não há bytes de alinhamento entre os
campos (ver [src/registros.c](src/registros.c)).

### `dados/produtos.dat` – 94 bytes por registro

| Campo | Tipo | Bytes | Observação |
|-------|------|------:|------------|
| `product_id` | long long | 8 | **chave**, sem repetição, ordem crescente |
| `category_id` | long long | 8 | repetido |
| `category_code` | texto | 40 | repetido (ex.: `electronics.smartphone`) |
| `brand` | texto | 32 | repetido (a maior marca do dataset tem 32 caracteres) |
| `price` | float | 4 | |
| `status` | char | 1 | `' '` ativo, `'*'` removido |
| `fim` | char | 1 | `'\n'` |

Gerado com um registro por `product_id` distinto do CSV (fica o primeiro que
aparece). Como o CSV não está ordenado por produto, a ordenação é **externa**:
o CSV é lido em blocos de 50.000 produtos, cada bloco é ordenado em memória
e gravado num arquivo temporário, e depois os blocos são intercalados (até 16 por
vez) descartando os repetidos. Assim o arquivo inteiro nunca fica na RAM.

### `dados/acessos.dat` – 97 bytes por registro

| Campo | Tipo | Bytes | Observação |
|-------|------|------:|------------|
| `id` | long long | 8 | **chave** sequencial gerada na importação (1, 2, 3, ...) |
| `event_time` | texto | 19 | `AAAA-MM-DD hh:mm:ss` (UTC) |
| `event_type` | texto | 16 | repetido: `view`, `cart`, `remove_from_cart`, `purchase` |
| `product_id` | long long | 8 | repetido; referência ao arquivo de produtos |
| `user_id` | long long | 8 | repetido |
| `user_session` | texto | 36 | repetido (UUID da sessão) |
| `status` | char | 1 | `' '` ativo, `'*'` removido |
| `fim` | char | 1 | `'\n'` |

Os eventos não têm identificador único no dataset, então a chave é um número
sequencial. O CSV já vem em ordem cronológica, então os ids saem ordenados.

**Amostra.** O mês de outubro tem cerca de 42 milhões de eventos (4 GB neste
formato), bem acima do limite de 100 MB por arquivo do GitHub. Por isso o
arquivo guarda uma **amostra sistemática de 200 mil eventos**. O programa conta
as linhas do CSV e importa 1 a cada *k* (*k* = total / 200.000, cerca de 212
para outubro). Assim a amostra cobre o mês inteiro e todas as horas do dia, e
não só as primeiras horas do dia 1º, como aconteceria lendo as primeiras linhas.

### Áreas de overflow: `dados/*_overflow.dat`

Cada arquivo de dados tem uma área de overflow com o mesmo formato de registro,
mantida **ordenada** pela chave e limitada a 100 registros.

## Arquivos de índice: `dados/produtos.idx` e `dados/acessos.idx`

Índice parcial de **2 níveis** sobre a área principal de cada arquivo:

```
[cabeçalho][nível 1: n1 entradas][nível 2: n2 entradas]

entrada = { chave (long long), pos (long long) }        16 bytes
```

- **Nível 1**: uma entrada a cada *B1* registros do arquivo de dados.
  `chave` = primeira chave do bloco, `pos` = número do registro.
- **Nível 2**: uma entrada a cada *B2* entradas do nível 1.
  `chave` = chave da entrada, `pos` = número da entrada no nível 1.
- **Cabeçalho**: tamanhos dos blocos, `n1`, `n2`, quantidade de registros
  indexados e o contador de remoções pendentes.

| Arquivo | B1 (registros por entrada do nível 1) | B2 (entradas do nível 1 por entrada do nível 2) |
|---------|------:|------:|
| produtos | 64 | 32 |
| acessos | 128 | 64 |

Com 200 mil acessos, o nível 1 tem 1.563 entradas (25 KB, fica no disco) e o
nível 2 tem 25 entradas (400 bytes, fica na memória). Com o mês de outubro
inteiro (cerca de 42 milhões de acessos) seriam cerca de 330 mil entradas no
nível 1 (5 MB) e 5.200 no nível 2 (83 KB). Um bloco de dados tem 6 KB (produtos) e
12 KB (acessos), ou seja, poucas páginas de disco.

**Carga e gravação.** Ao abrir o programa, o cabeçalho e o nível 2 são carregados
em memória. Se o índice não existe ou não confere com o arquivo de dados, ele é
reconstruído. Ao sair, o índice é gravado.

**Consulta pelo índice** ([src/indice.c](src/indice.c), `indice_bloco`):
1. pesquisa binária no nível 2 (memória) → intervalo de entradas do nível 1;
2. pesquisa binária no nível 1 **direto no arquivo de índice** (`seek`) → bloco
   de dados;
3. `seek` + pesquisa binária dentro do bloco no arquivo de dados;
4. se não encontrou, pesquisa binária na área de overflow.

O programa mostra quantas leituras em disco cada pesquisa fez. Para comparar, há
também a **pesquisa binária direta** no arquivo de dados inteiro, sem índice.

## Inserção, remoção e reconstrução do índice

- **Inserção**: o registro vai para a área de overflow na posição ordenada; os
  maiores são deslocados uma posição. A área principal e o índice não mudam.
  Se a chave pertence a um registro removido, o mesmo espaço é reaproveitado.
  Nos acessos o `id` é gerado automaticamente (maior id + 1), e o `product_id`
  precisa existir no arquivo de produtos.
- **Remoção**: lógica. O registro é marcado com `'*'` no próprio arquivo e
  passa a ser ignorado pelas consultas.
- **Reorganização**: acontece quando o overflow chega a **100 registros** ou
  quando há **100 remoções pendentes**. A área principal e o overflow são
  intercalados num arquivo novo, um registro de cada vez, sem os removidos. O
  overflow é esvaziado e o **índice é reconstruído**. Também pode ser disparada
  pelo menu. Entre reorganizações, o índice continua válido porque a área
  principal não muda de posição.

## Como compilar e executar

Precisa do `gcc` (no Windows, MinGW-w64/WinLibs). Execute na pasta raiz do projeto:

```bash
gcc -std=c99 -Wall -Wextra -O2 -o ecommerce.exe src/main.c
```

Só o `main.c` é compilado: ele inclui os outros `.c` (`#include "tabela.c"` etc.).
No Dev-C++, coloque no projeto apenas o `src/main.c` e salve o `.dev` na raiz.

```bash
./ecommerce.exe
```

Também dá para usar `compilar.bat` ou `make`. Na primeira execução o programa
pede o caminho do CSV (por exemplo `dados/2019-Oct.csv`, baixado do Kaggle) e
quantas linhas importar. O padrão (`0`) é a amostra de **200 mil linhas**
espalhadas pelo CSV: o `acessos.dat` fica com cerca de 19 MB. `-1` importa o
CSV inteiro. O CSV original não vai para o
repositório (`.gitignore`).

## Estrutura do código

Os arquivos estão listados na ordem em que o `main.c` os inclui: cada um usa
apenas o que vem antes dele.

| Arquivo | Conteúdo |
|---------|----------|
| [src/config.c](src/config.c) | caminhos, tamanhos de bloco, limites de reorganização |
| [src/arquivo.c](src/arquivo.c) | leitura e gravação de registro *n* com `seek`, textos de tamanho fixo |
| [src/registros.c](src/registros.c) | structs `Produto` e `Acesso`, leitura do CSV, impressão |
| [src/entrada.c](src/entrada.c) | leitura do teclado |
| [src/indice.c](src/indice.c) | índice parcial de 2 níveis |
| [src/tabela.c](src/tabela.c) | organização sequencial-indexada genérica: pesquisas, inserção, remoção, reorganização, listagem |
| [src/importacao.c](src/importacao.c) | carga inicial a partir do CSV e ordenação externa |
| [src/consultas.c](src/consultas.c) | as três perguntas |
| [src/main.c](src/main.c) | menus; inclui todos os arquivos acima |
