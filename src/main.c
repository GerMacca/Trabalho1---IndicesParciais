#define __USE_MINGW_ANSI_STDIO 1
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <ctype.h>
#include "config.c"
#include "arquivo.c"
#include "registros.c"
#include "entrada.c"
#include "indice.c"
#include "tabela.c"
#include "importacao.c"
#include "consultas.c"

static Tabela produtos;
static Tabela acessos;

static int ler_novo_produto(void *reg){
    Produto *p = reg;
    char buf[128];

    p->product_id = ler_chave("product_id: ");
    if (p->product_id <= 0) {   // letra ou numero invalido: strtoll devolve 0
        printf("product_id invalido: insercao cancelada.\n");
        return 0;
    }
    p->category_id = ler_chave("category_id: ");
    ler_texto("category_code (ex.: electronics.smartphone): ", buf, sizeof buf);
    texto_fixo(p->category_code, TAM_CATEGORIA, buf);
    ler_texto("brand: ", buf, sizeof buf);
    texto_fixo(p->brand, TAM_MARCA, buf);
    p->price = (float)ler_real("price: ");
    return 1;
}

static void sugestao_imprimir(const Produto *p){
    char marca[TAM_MARCA + 1], categoria[TAM_CATEGORIA + 1];

    texto_ler(marca, p->brand, TAM_MARCA);
    texto_ler(categoria, p->category_code, TAM_CATEGORIA);
    printf("  %-10lld %-20.20s %-30.30s %9.2f\n", p->product_id,
           marca[0] ? marca : "-", categoria[0] ? categoria : "-", p->price);
}

/* Mostra ate 10 produtos para ajudar a escolher um product_id
   filtro vazio: produtos sorteados (seek em posicoes aleatorias do arquivo)
   filtro com texto: produtos cuja marca contem o texto (le o arquivo em sequencia) */
static void sugerir_produtos(const char *filtro){
    Produto p;
    Iterador it;
    char busca[64], marca[TAM_MARCA + 1];
    int i, mostrados = 0, tentativas;

    for (i = 0; filtro[i] && i < 63; i++)
        busca[i] = (char)tolower((unsigned char)filtro[i]);   // marcas estao em minusculas
    busca[i] = '\0';

    printf("  %-10s %-20s %-30s %9s\n", "product_id", "brand", "category_code", "price");
    if (!busca[0]) {
        for (tentativas = 0; mostrados < 10 && tentativas < 100 && produtos.n_principal > 0; tentativas++) {
            // rand() vai so ate 32767 no Windows: dois sorteios cobrem o arquivo todo
            long n = (rand() * (RAND_MAX + 1L) + rand()) % produtos.n_principal;

            if (arq_ler(produtos.dados, n, &p, sizeof p) && p.status != STATUS_REMOVIDO) {
                sugestao_imprimir(&p);
                mostrados++;
            }
        }
    } else if (iter_iniciar(&it, &produtos)) {
        while (mostrados < 10 && iter_proximo(&it, &p)) {
            texto_ler(marca, p.brand, TAM_MARCA);
            if (strstr(marca, busca)) {
                sugestao_imprimir(&p);
                mostrados++;
            }
        }
        iter_fim(&it);
    }
    if (mostrados == 0)
        printf("  nenhum produto encontrado\n");
}

static int ler_novo_acesso(void *reg){
    Acesso *a = reg;
    Produto p;
    char buf[128];
    time_t agora = time(NULL);

    a->id = tabela_maior_chave(&acessos) + 1;
    printf("Novo id: %lld\n", a->id);

    ler_texto("event_type (view, cart, remove_from_cart, purchase): ", buf, sizeof buf);
    texto_fixo(a->event_type, TAM_EVENTO, buf);

    // "?" mostra produtos sorteados; "?samsung" busca pela marca
    for (;;) {
        ler_texto("product_id (? = sugestoes, ?marca = buscar por marca): ", buf, sizeof buf);
        if (buf[0] != '?')
            break;
        sugerir_produtos(buf + 1);
    }
    a->product_id = strtoll(buf, NULL, 10);
    if (!tabela_busca_indice(&produtos, a->product_id, &p)) {
        printf("Produto %lld nao existe no arquivo de produtos.\n", a->product_id);
        return 0;
    }
    a->user_id = ler_chave("user_id: ");
    ler_texto("user_session: ", buf, sizeof buf);
    texto_fixo(a->user_session, TAM_SESSAO, buf);

    strftime(buf, sizeof buf, "%Y-%m-%d %H:%M:%S", gmtime(&agora));
    texto_fixo(a->event_time, TAM_DATA, buf);
    return 1;
}

//====== Interface do menu =========
static void pausar(void){
    char buf[8];

    ler_texto("\n[Enter para continuar]", buf, sizeof buf);
}

static void pesquisar(Tabela *t, int usar_indice){
    char reg[256];
    long long chave = ler_chave("Chave: ");
    clock_t ini = clock();
    int achou = usar_indice ? tabela_busca_indice(t, chave, reg) : tabela_busca_binaria(t, chave, reg);
    double ms = 1000.0 * (double)(clock() - ini) / CLOCKS_PER_SEC;

    if (achou) {
        t->cabecalho();
        t->imprimir(reg);
    } else {
        printf("Chave %lld nao encontrada.\n", chave);
    }
    printf("Leituras em disco: %ld  |  tempo: %.3f ms\n", t->leituras, ms);
}

static void inserir(Tabela *t, int (*ler_novo)(void *))
{
    char reg[256];

    if (!ler_novo(reg))
        return;
    switch (tabela_inserir(t, reg)) {
	    case 1:  printf("Registro inserido.\n"); break;
	    case 0:  printf("Ja existe um registro com essa chave.\n"); break;
	    default: printf("Erro ao inserir.\n"); break;
    }
}

static void remover(Tabela *t)
{
    long long chave = ler_chave("Chave a remover: ");

    if (tabela_remover(t, chave))
        printf("Registro %lld removido (remocao logica).\n", chave);
    else
        printf("Chave %lld nao encontrada.\n", chave);
}

static void menu_tabela(Tabela *t, int (*ler_novo)(void *))
{
    long op;

    do {
        printf("\n===== %s =====\n", t->nome);
        printf("1 - Mostrar dados (parcial)\n");
        printf("2 - Mostrar todos os dados\n");
        printf("3 - Mostrar area de overflow\n");
        printf("4 - Pesquisa binaria direto no arquivo de dados\n");
        printf("5 - Pesquisa pelo indice parcial\n");
        printf("6 - Inserir\n");
        printf("7 - Remover\n");
        printf("8 - Reorganizar agora\n");
        printf("9 - Informacoes dos arquivos e do indice\n");
        printf("0 - Voltar\n");
        op = ler_inteiro("> ");

        switch (op) {
        case 1: {
            long inicio = ler_inteiro("A partir do registro (0 = primeiro): ");
            long qtd = ler_inteiro("Quantidade: ");
            tabela_mostrar(t, inicio, qtd > 0 ? qtd : 20);
            break;
        }
        case 2: tabela_mostrar(t, 0, 0); break;
        case 3: tabela_mostrar_overflow(t); break;
        case 4: pesquisar(t, 0); break;
        case 5: pesquisar(t, 1); break;
        case 6: inserir(t, ler_novo); break;
        case 7: remover(t); break;
        case 8: tabela_reorganizar(t); break;
        case 9: tabela_info(t); break;
        case 0: break;
        default: printf("Opcao invalida.\n"); break;
        }
        if (op != 0)
            pausar();
    } while (op != 0);
}

static void menu_consultas(void)
{
    long op;

    do {
        printf("\n===== Consultas =====\n");
        printf("1 - Quais marcas sao as mais compradas?\n");
        printf("2 - Em que horario acontecem mais compras?\n");
        printf("3 - Qual a taxa de conversao (visualizacao -> compra)?\n");
        printf("4 - Quais categorias geram mais faturamento? (extra)\n");
        printf("5 - Em que dia da semana se compra mais? (extra)\n");
        printf("0 - Voltar\n");
        op = ler_inteiro("> ");

        switch (op) {
        case 1: consulta_marcas_mais_compradas(&produtos, &acessos, 15); break;
        case 2:
            printf("1 - Grafico de barras\n");
            printf("2 - Valores\n");
            consulta_compras_por_hora(&acessos, ler_inteiro("> ") == 2);
            break;
        case 3: consulta_funil_conversao(&acessos); break;
        case 4: consulta_categorias_faturamento(&produtos, &acessos); break;
        case 5: consulta_dia_da_semana(&acessos); break;
        case 0: break;
        default: printf("Opcao invalida.\n"); break;
        }
        if (op != 0)
            pausar();
    } while (op != 0);
}

static int importar(void)
{
    char caminho[512];
    long limite, n_acessos, n_produtos;

    ler_texto("Caminho do CSV (ex.: dados/2019-Oct.csv): ", caminho, sizeof caminho);
    limite = ler_inteiro("Quantidade de linhas a importar, espalhadas pelo CSV "
                         "(0 = padrao de 200000, -1 = todas): ");
    if (limite == 0)
        limite = LIMITE_LINHAS_PADRAO;

    if (!importar_csv(caminho, limite, &n_acessos, &n_produtos))
        return 0;
    printf("Importados %ld acessos e %ld produtos distintos.\n",
           n_acessos, n_produtos);
    return 1;
}

static int abrir_tabelas(void)
{
    return tabela_abrir(&produtos) && tabela_abrir(&acessos);
}

static void fechar_tabelas(void)
{
    tabela_fechar(&produtos);
    tabela_fechar(&acessos);
}

int main(void)
{
    long op;

    srand((int)time(NULL));   // sorteio das sugestoes de produto muda a cada execucao
    tabela_configurar(&produtos, "Produtos", ARQ_PRODUTOS, ARQ_PRODUTOS_OVERFLOW,
                      ARQ_PRODUTOS_INDICE, sizeof(Produto),
                      PRODUTOS_BLOCO1, PRODUTOS_BLOCO2,
                      produto_cabecalho, produto_imprimir);
    tabela_configurar(&acessos, "Acessos", ARQ_ACESSOS, ARQ_ACESSOS_OVERFLOW,
                      ARQ_ACESSOS_INDICE, sizeof(Acesso),
                      ACESSOS_BLOCO1, ACESSOS_BLOCO2,
                      acesso_cabecalho, acesso_imprimir);

    while (!arq_existe(ARQ_PRODUTOS) || !arq_existe(ARQ_ACESSOS)) {
        printf("Arquivos de dados nao encontrados em %s.\n",
               DIR_DADOS);
        if (!importar())
            return 1;
    }
    if (!abrir_tabelas()) {
        printf("Erro ao abrir os arquivos de dados.\n");
        return 1;
    }

    do {
        printf("\n===== E-commerce: organizacao sequencial-indexada =====\n");
        printf("1 - Arquivo de produtos\n");
        printf("2 - Arquivo de acessos\n");
        printf("3 - Consultas\n");
        printf("4 - Importar CSV (recriar os arquivos de dados)\n");
        printf("0 - Sair\n");
        op = ler_inteiro("> ");

        switch (op) {
        case 1: menu_tabela(&produtos, ler_novo_produto); break;
        case 2: menu_tabela(&acessos, ler_novo_acesso); break;
        case 3: menu_consultas(); break;
        case 4:
            fechar_tabelas();
            importar();
            if (!abrir_tabelas()) {
                printf("Erro ao abrir os arquivos de dados.\n");
                return 1;
            }
            break;
        case 0: break;
        default: printf("Opcao invalida.\n"); break;
        }
    } while (op != 0);

    fechar_tabelas();
    printf("Indices gravados!\n");
    return 0;
}
