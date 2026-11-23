#include "gerador.h"
#include <stdlib.h>
#include <string.h>

static FILE *fp_mepa = NULL;
static int contador_rotulos = 0;

bool gerador_init(const char *nome_arquivo_saida) {
    fp_mepa = fopen(nome_arquivo_saida, "w");
    if (!fp_mepa) return false;
    contador_rotulos = 0;
    return true;
}

void gerador_encerrar(void) {
    if (fp_mepa) {
        fflush(fp_mepa);
        fclose(fp_mepa);
        fp_mepa = NULL;
    }
}

char* novo_rotulo(void) {
    static char rotulo[16];
    contador_rotulos++;
    snprintf(rotulo, sizeof(rotulo), "L%d", contador_rotulos);
    return rotulo;
}

void gera_instr_mepa(const char *rotulo, const char *mnemonico, const char *parametro1, const char *parametro2) {
    if (!fp_mepa || !mnemonico) return;

    char linha[128];
    char pfx[32] = "";

    if (rotulo && rotulo[0] != '\0') {
        char rot_com_dois_pontos[20];
        snprintf(rot_com_dois_pontos, sizeof(rot_com_dois_pontos), "%s:", rotulo);
        snprintf(pfx, sizeof(pfx), "%-7s ", rot_com_dois_pontos);
    } else {
        snprintf(pfx, sizeof(pfx), "        ");
    }

    if (parametro1 && parametro2) {
        snprintf(linha, sizeof(linha), "%s%-6s %s,%s", pfx, mnemonico, parametro1, parametro2);
    } else if (parametro1) {
        snprintf(linha, sizeof(linha), "%s%-6s %s", pfx, mnemonico, parametro1);
    } else {
        snprintf(linha, sizeof(linha), "%s%-6s", pfx, mnemonico);
    }

    fprintf(fp_mepa, "%s\n", linha);
}