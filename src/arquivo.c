#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>

/* Funções de acesso a registros de tamanho fixo em arquivos binários.
   Os registros são endereçados pelo número (0, 1, 2, ...); o endereço
   em bytes é numero * tamanho. */

int     arq_seek(FILE *f, int64_t byte);
int64_t arq_qtd_registros(FILE *f, size_t tam);
int     arq_ler(FILE *f, int64_t n, void *reg, size_t tam);
int     arq_gravar(FILE *f, int64_t n, const void *reg, size_t tam);
int64_t arq_tamanho_bytes(const char *caminho);
int     arq_existe(const char *caminho);

/* Copia orig para um campo de texto de tamanho fixo, completando com
   espaços (o campo não tem '\0'). */
void texto_fixo(char *campo, size_t tam, const char *orig);

/* Copia um campo de texto fixo para dest (tam + 1 bytes), removendo os
   espaços do final. */
void texto_ler(char *dest, const char *campo, size_t tam);

/* Arquivos maiores que 2 GB precisam de seek de 64 bits. */
#ifdef _WIN32
#define FSEEK64 _fseeki64
#define FTELL64 _ftelli64
#else
#define FSEEK64 fseeko
#define FTELL64 ftello
#endif

int arq_seek(FILE *f, int64_t byte)
{
    return FSEEK64(f, byte, SEEK_SET) == 0;
}

int64_t arq_qtd_registros(FILE *f, size_t tam)
{
    if (FSEEK64(f, 0, SEEK_END) != 0)
        return 0;
    return FTELL64(f) / (int64_t)tam;
}

int arq_ler(FILE *f, int64_t n, void *reg, size_t tam)
{
    if (!arq_seek(f, n * (int64_t)tam))
        return 0;
    return fread(reg, tam, 1, f) == 1;
}

int arq_gravar(FILE *f, int64_t n, const void *reg, size_t tam)
{
    if (!arq_seek(f, n * (int64_t)tam))
        return 0;
    if (fwrite(reg, tam, 1, f) != 1)
        return 0;
    fflush(f);
    return 1;
}

int64_t arq_tamanho_bytes(const char *caminho)
{
    FILE *f = fopen(caminho, "rb");
    int64_t tam;

    if (!f)
        return 0;
    tam = arq_qtd_registros(f, 1);
    fclose(f);
    return tam;
}

int arq_existe(const char *caminho)
{
    FILE *f = fopen(caminho, "rb");

    if (!f)
        return 0;
    fclose(f);
    return 1;
}

void texto_fixo(char *campo, size_t tam, const char *orig)
{
    size_t n = strlen(orig);

    if (n > tam)
        n = tam;
    memcpy(campo, orig, n);
    memset(campo + n, ' ', tam - n);
}

void texto_ler(char *dest, const char *campo, size_t tam)
{
    memcpy(dest, campo, tam);
    dest[tam] = '\0';
    while (tam > 0 && dest[tam - 1] == ' ')
        dest[--tam] = '\0';
}
