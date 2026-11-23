#include "symtab.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_SYMBOLS 1024
#define MAX_SCOPES  64

static Simbolo tabela[MAX_SYMBOLS];
static int total_simbolos = 0;

static char pilha_escopos[MAX_SCOPES][64];
static int topo_escopo = -1;

void symtab_init(void) {
    total_simbolos = 0;
    topo_escopo = -1;
    for (int i = 0; i < MAX_SYMBOLS; i++) {
        tabela[i].ocupado = false;
    }
    symtab_enter_scope("global");
}

void symtab_destroy(void) {
    total_simbolos = 0;
    topo_escopo = -1;
}

void symtab_enter_scope(const char *name) {
    if (topo_escopo < MAX_SCOPES - 1) {
        topo_escopo++;
        strncpy(pilha_escopos[topo_escopo], name, 63);
        pilha_escopos[topo_escopo][63] = '\0';
    }
}

void symtab_leave_scope(void) {
    if (topo_escopo > 0) {
        topo_escopo--;
    }
}

int symtab_get_current_level(void) {
    return (topo_escopo <= 0) ? 0 : 1;
}

const char *symtab_get_current_scope_name(void) {
    return (topo_escopo >= 0) ? pilha_escopos[topo_escopo] : "global";
}

Simbolo *symtab_lookup_current_scope(const char *id) {
    if (!id || topo_escopo < 0) return NULL;
    const char *escopo_atual = pilha_escopos[topo_escopo];

    for (int i = 0; i < total_simbolos; i++) {
        if (tabela[i].ocupado &&
            strcmp(tabela[i].scope, escopo_atual) == 0 &&
            strcmp(tabela[i].id, id) == 0) {
            return &tabela[i];
        }
    }
    return NULL;
}

Simbolo *symtab_lookup(const char *id) {
    if (!id || topo_escopo < 0) {
        return NULL;
    }

    for (int sc = topo_escopo; sc >= 0; sc--) {
        for (int i = total_simbolos - 1; i >= 0; i--) {
            if (tabela[i].ocupado &&
                strcmp(tabela[i].scope, pilha_escopos[sc]) == 0 &&
                strcmp(tabela[i].id, id) == 0) {
                return &tabela[i];
            }
        }
    }
    return NULL;
}

bool symtab_insert(const char *id, SimboloCat cat, SimboloTipo tipo, int offset, int extra) {
    if (symtab_lookup_current_scope(id) != NULL) {
        return false;
    }

    if (total_simbolos >= MAX_SYMBOLS) return false;

    Simbolo *s = &tabela[total_simbolos++];
    s->ocupado = true;
    strncpy(s->id, id, 31);
    s->id[31] = '\0';
    s->cat = cat;
    s->tipo = tipo;
    s->extra = extra;
    strncpy(s->scope, symtab_get_current_scope_name(), 63);
    s->scope[63] = '\0';

    if (cat == CAT_VAR_GLOBAL) {
        s->level = 0;
    } else {
        s->level = symtab_get_current_level();
    }

    s->offset = offset;
    s->tam_vetor = (extra > 0) ? extra : 0;
    s->rotulo[0] = '\0';
    s->qtd_params = 0;

    return true;
}

void symtab_update_pending_types(SimboloTipo tipo, int tam_vetor) {
    const char *escopo_atual = symtab_get_current_scope_name();
    for (int i = 0; i < total_simbolos; i++) {
        if (tabela[i].ocupado &&
            strcmp(tabela[i].scope, escopo_atual) == 0 &&
            tabela[i].tipo == TIPO_INDEF) {
            tabela[i].tipo = tipo;
            tabela[i].tam_vetor = tam_vetor;
        }
    }
}

void symtab_update_signature(const char *id, SimboloTipo type, int extra) {
    Simbolo *s = symtab_lookup(id);
    if (s) {
        s->tipo = type;
        s->extra = extra;
    }
}

int symtab_get_count(void) {
    return total_simbolos;
}

const SymEntry *symtab_get_entry(int index) {
    if (index >= 0 && index < total_simbolos) {
        return &tabela[index];
    }
    return NULL;
}

const char *symtab_cat_to_str(SimboloCat cat) {
    switch (cat) {
        case CAT_VAR_GLOBAL:   return "VAR_GLOBAL";
        case CAT_VAR_LOCAL:    return "VAR_LOCAL";
        case CAT_PARAM:        return "PARAM";
        case CAT_PROCEDIMENTO: return "PROCEDIMENTO";
        case CAT_FUNCAO:       return "FUNCAO";
        default:               return "INDEF";
    }
}

const char *symtab_type_to_str(SimboloTipo type) {
    switch (type) {
        case TIPO_INT:   return "int";
        case TIPO_BOOL:  return "logic";
        case TIPO_CHR:   return "chr";
        case TIPO_VOID:  return "void";
        default:         return "indef";
    }
}

void symtab_print(void) {
    printf("\n=== TABELA DE SIMBOLOS (COMPLETA) ===\n");
    for (int i = 0; i < total_simbolos; i++) {
        printf("[%02d] %-12s | %-12s | %-6s | escopo: %-8s | nivel: %d | offset: %d\n",
               i, tabela[i].id,
               symtab_cat_to_str(tabela[i].cat),
               symtab_type_to_str(tabela[i].tipo),
               tabela[i].scope,
               tabela[i].level,
               tabela[i].offset);
    }
    printf("=====================================\n\n");
}