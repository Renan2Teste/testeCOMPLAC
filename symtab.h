#ifndef SYMTAB_H
#define SYMTAB_H

#include <stdbool.h>

#define MAX_PARAMS 32

typedef enum {
    CAT_VAR_GLOBAL,
    CAT_VAR_LOCAL,
    CAT_PARAM,
    CAT_PROCEDIMENTO,
    CAT_FUNCAO,
    CAT_INDEF
} SimboloCat;

typedef enum {
    TIPO_INT,
    TIPO_BOOL,
    TIPO_CHR,
    TIPO_VOID,
    TIPO_INDEF
} SimboloTipo;

typedef struct {
    SimboloTipo tipo;
    bool eh_referencia;
} ParamInfo;

typedef struct {
    bool ocupado;
    bool eh_referencia;
    char id[32];
    SimboloCat cat;
    SimboloTipo tipo;
    int extra;
    char scope[64];
    int level;
    int offset;
    int tam_vetor;
    char rotulo[16];
    int qtd_params;
    ParamInfo params[MAX_PARAMS];
} Simbolo;

typedef Simbolo SymEntry;

void symtab_init(void);
void symtab_destroy(void);

void symtab_enter_scope(const char *name);
void symtab_leave_scope(void);

// AQUI: 5 parâmetros obrigatórios
bool symtab_insert(const char *id, SimboloCat cat, SimboloTipo tipo, int offset, int extra);

Simbolo *symtab_lookup(const char *id);
Simbolo *symtab_lookup_current_scope(const char *id);

int symtab_get_count(void);
int symtab_get_current_level(void);
const char *symtab_get_current_scope_name(void);
const SymEntry *symtab_get_entry(int index);

const char *symtab_cat_to_str(SimboloCat cat);
const char *symtab_type_to_str(SimboloTipo type);

void symtab_update_signature(const char *id, SimboloTipo type, int extra);
void symtab_update_pending_types(SimboloTipo tipo, int tam_vetor);

void symtab_print(void);

#endif // SYMTAB_H