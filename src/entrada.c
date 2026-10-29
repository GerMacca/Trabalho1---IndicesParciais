#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Leitura do teclado linha a linha (evita os problemas do scanf com o
   '\n' que sobra no buffer). */
void     ler_texto(const char *msg, char *buf, size_t tam);
int64_t  ler_inteiro(const char *msg);
uint64_t ler_chave(const char *msg);
double   ler_real(const char *msg);

void ler_texto(const char *msg, char *buf, size_t tam)
{
    printf("%s", msg);
    fflush(stdout);
    if (!fgets(buf, (int)tam, stdin)) {
        buf[0] = '\0';
        return;
    }
    buf[strcspn(buf, "\r\n")] = '\0';
}

int64_t ler_inteiro(const char *msg)
{
    char buf[64];

    ler_texto(msg, buf, sizeof buf);
    return strtoll(buf, NULL, 10);
}

uint64_t ler_chave(const char *msg)
{
    char buf[64];

    ler_texto(msg, buf, sizeof buf);
    return strtoull(buf, NULL, 10);
}

double ler_real(const char *msg)
{
    char buf[64];

    ler_texto(msg, buf, sizeof buf);
    return strtod(buf, NULL);
}
