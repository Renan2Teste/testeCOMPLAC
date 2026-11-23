#ifndef GERADOR_H
#define GERADOR_H

#include <stdio.h>
#include <stdbool.h>

bool gerador_init(const char *nome_arquivo_saida);
void gerador_encerrar(void);

char* novo_rotulo(void);

// Emite instrução MEPA. Se houver 2 parâmetros, formata sem espaço após a vírgula (ex: ARMZ 0,0)
void gera_instr_mepa(const char *rotulo, const char *mnemonico, const char *parametro1, const char *parametro2);

#endif // GERADOR_H