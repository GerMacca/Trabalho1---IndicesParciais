#include <stdio.h>
#include <string.h>

int       arq_seek(FILE *f, long long byte);
long      arq_qtd_registros(FILE *f, int tam);
int       arq_ler(FILE *f, long n, void *reg, int tam);
int       arq_gravar(FILE *f, long n, const void *reg, int tam);
long long arq_tamanho_bytes(const char *caminho);
int       arq_existe(const char *caminho);

void texto_fixo(char *campo, int tam, const char *orig);

void texto_ler(char *dest, const char *campo, int tam);

int arq_seek(FILE *f, long long byte)
{
    return _fseeki64(f, byte, SEEK_SET) == 0;
}

long arq_qtd_registros(FILE *f, int tam)
{
    if (_fseeki64(f, 0, SEEK_END) != 0)
        return 0;
    return (long)(_ftelli64(f) / tam);
}

int arq_ler(FILE *f, long n, void *reg, int tam)
{
    if (!arq_seek(f, (long long)n * tam))
        return 0;
    return fread(reg, tam, 1, f) == 1;
}

int arq_gravar(FILE *f, long n, const void *reg, int tam)
{
    if (!arq_seek(f, (long long)n * tam))
        return 0;
    if (fwrite(reg, tam, 1, f) != 1)
        return 0;
    fflush(f);
    return 1;
}

long long arq_tamanho_bytes(const char *caminho)
{
    FILE *f = fopen(caminho, "rb");
    long long tam = 0;

    if (!f)
        return 0;
    if (_fseeki64(f, 0, SEEK_END) == 0)
        tam = _ftelli64(f);
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

void texto_fixo(char *campo, int tam, const char *orig)
{
    int i = 0;

    while (i < tam && orig[i] != '\0') {
        campo[i] = orig[i];
        i++;
    }
    while (i < tam)
        campo[i++] = ' ';
}

void texto_ler(char *dest, const char *campo, int tam)
{
    int i;

    for (i = 0; i < tam; i++)
        dest[i] = campo[i];
    dest[tam] = '\0';
    while (tam > 0 && dest[tam - 1] == ' ')
        dest[--tam] = '\0';
}
