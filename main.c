#include <stdio.h>
#include <string.h>
#include "lex.h"
#include "parser.h"
#include "symtab.h"
#include "gerador.h"
#include "diag.h"
#include "opt.h"

int main(int argc, char *argv[]) {
    // 1. Processa linha de comando
    opts_parse(argc, argv);
    const Options *opts = opts_get();
    const char *arquivo_fonte = opts->filename;

    if (!arquivo_fonte) {
        fprintf(stderr, "Uso: complac <arquivo.slac> [opcoes]\n");
        return 1;
    }

    // 2. Monta o nome do arquivo de saída trocando a extensão por .mepa
    char arquivo_saida[256];
    strncpy(arquivo_saida, arquivo_fonte, sizeof(arquivo_saida) - 1);
    arquivo_saida[sizeof(arquivo_saida) - 1] = '\0';
    char *ext = strrchr(arquivo_saida, '.');
    if (ext) {
        strcpy(ext, ".mepa");
    } else {
        strncat(arquivo_saida, ".mepa", sizeof(arquivo_saida) - strlen(arquivo_saida) - 1);
    }

    // 3. Inicializa os módulos
    lex_init(arquivo_fonte);
    symtab_init();
    if (!gerador_init(arquivo_saida)) {
        fprintf(stderr, "Erro ao criar arquivo MEPA: %s\n", arquivo_saida);
        return 1;
    }

    // 4. Executa a compilação completa (Análise + Emissão MEPA)
    parse_program();

    // 5. Finaliza e descarrega os arquivos
    gerador_encerrar();
    symtab_destroy();
    lex_close();

    printf("Compilacao concluida com sucesso! Gerado: %s\n", arquivo_saida);
    return 0;
}