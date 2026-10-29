#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void      ler_texto(const char *msg, char *buf, int tam);
long      ler_inteiro(const char *msg);
long long ler_chave(const char *msg);
double    ler_real(const char *msg);

void ler_texto(const char *msg, char *buf, int tam)
{
    printf("%s", msg);
    fflush(stdout);
    if (!fgets(buf, tam, stdin)) {
        buf[0] = '\0';
        return;
    }
    buf[strcspn(buf, "\r\n")] = '\0';
}

long ler_inteiro(const char *msg)
{
    char buf[64];

    ler_texto(msg, buf, sizeof buf);
    return strtol(buf, NULL, 10);
}

long long ler_chave(const char *msg)
{
    char buf[64];

    ler_texto(msg, buf, sizeof buf);
    return strtoll(buf, NULL, 10);
}

double ler_real(const char *msg)
{
    char buf[64];

    ler_texto(msg, buf, sizeof buf);
    return strtod(buf, NULL);
}
