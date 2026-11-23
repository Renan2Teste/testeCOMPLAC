#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include "parser.h"
#include "lex.h"
#include "diag.h"
#include "symtab.h"
#include "gerador.h"

static Token current_tok;
static Token lookahead_tok;
static int has_lookahead = 0;
static int saw_main = 0;

/* Gerenciamento de endereçamento na pilha da MEPA */
static int offset_global = 0;
static int offset_local = 0;
static char escopo_atual_nome[64] = "global";
static int func_atual_nparams = 0;
static bool dentro_de_funcao = false;
static bool param_refs[MAX_PARAMS];
static SimboloTipo retorno_tipo;
static bool houve_retorno;
static char retorno_rotulo[16];
static int choose_offsets[64], choose_depth;


static char* int_to_str(int n) {
    static char bufs[4][16];
    static int idx = 0;
    char *buf = bufs[idx];
    idx = (idx + 1) % 4;
    snprintf(buf, 16, "%d", n);
    return buf;
}

static void advance(void) {
    if (has_lookahead) {
        current_tok = lookahead_tok;
        has_lookahead = 0;
    } else {
        current_tok = lex_next();
    }
}

static Token peek(void) {
    if (!has_lookahead) {
        lookahead_tok = lex_next();
        has_lookahead = 1;
    }
    return lookahead_tok;
}

static void check(TokenCategoria expected) {
    if (current_tok.cat == expected) {
        advance();
    } else {
        diag_error_syntax(
            current_tok.line,
            tokenCatNome(expected),
            tokenCatNome(current_tok.cat)
        );
    }
}

static SimboloTipo parse_expr(void);
static SimboloTipo parse_elogc(void);
static SimboloTipo parse_erlac(void);
static SimboloTipo parse_earit(void);
static SimboloTipo parse_earip(void);
static SimboloTipo parse_factor(void);
static SimboloTipo parse_atom(void);
static SimboloTipo parse_elem(void);

static void parse_cmd(void);
static void parse_block(void);
static void parse_cmd_list(void);

static void parse_gvars(void);
static void parse_lvars(void);
static void parse_decls(SimboloCat cat);
static SimboloTipo parse_type(int *extra_out);
static void parse_id(void);

static void parse_subs(void);
static void parse_func(void);
static void parse_proc(void);
static int  parse_param(char nomes[][32], SimboloTipo tipos[], int extras[]);

static void parse_echo(void);
static void parse_get(void);
static void parse_case(void);
static void parse_chse(void);
static void parse_mlst(const char *rotulo_fim);
static void parse_mtch(const char *rotulo_fim);
static void parse_mvlrs(void);
static void parse_othr(void);
static void parse_vetr(void);
static void parse_for(void);
static void parse_whle(void);
static void parse_rept(void);
static void parse_call(void);
static void parse_ret(void);
static void parse_atr(void);

static Simbolo* require_declared(const char *name, int line) {
    Simbolo *s = symtab_lookup(name);
    if (s == NULL) {
        char msg[160];
        snprintf(msg, sizeof(msg), "identificador '%s' nao declarado", name);
        diag_error(line, msg);
    }
    return s;
}

static Simbolo* parse_use_id(void) {
    char name[32];
    int line = current_tok.line;
    strncpy(name, current_tok.lexeme, sizeof(name) - 1);
    name[sizeof(name) - 1] = '\0';
    parse_id();
    return require_declared(name, line);
}

#define PARSE_DECLS_MAX_IDS 64
static void parse_decls(SimboloCat cat) {
    char nomes[PARSE_DECLS_MAX_IDS][32];
    int n = 0;
    int i;
    SimboloTipo tipo;
    int extra = 0;

    strncpy(nomes[n], current_tok.lexeme, sizeof(nomes[n]) - 1);
    nomes[n][sizeof(nomes[n]) - 1] = '\0';
    n++;
    parse_id();

    while (current_tok.cat == sVIRG) {
        check(sVIRG);
        if (n < PARSE_DECLS_MAX_IDS) {
            strncpy(nomes[n], current_tok.lexeme, sizeof(nomes[n]) - 1);
            nomes[n][sizeof(nomes[n]) - 1] = '\0';
            n++;
        }
        parse_id();
    }

    check(sKIND);
    tipo = parse_type(&extra);
    check(sPONTOVIRG);

    for (i = 0; i < n; i++) {
        int desloc;
        int espaco = (extra > 0) ? (extra + 1) : 1;

        if (cat == CAT_VAR_GLOBAL) {
            desloc = offset_global;
            offset_global += espaco;
        } else {
            desloc = offset_local;
            offset_local += espaco;
        }

        if (!symtab_insert(nomes[i], cat, tipo, desloc, extra)) {
            diag_error_syntax(current_tok.line, "identificador duplicado no escopo", nomes[i]);
        }
    }
}

static void parse_gvars(void) {
    check(sGLOBVARS);
    int offset_antes = offset_global;
    parse_decls(CAT_VAR_GLOBAL);
    while (current_tok.cat == sIDENTIF) {
        parse_decls(CAT_VAR_GLOBAL);
    }
    int total_alocado = offset_global - offset_antes;
    if (total_alocado > 0) {
        gera_instr_mepa(NULL, "AMEM", int_to_str(total_alocado), NULL);
    }
}

static void parse_lvars(void) {
    check(sLOCVARS);
    int offset_antes = offset_local;
    parse_decls(CAT_VAR_LOCAL);
    while (current_tok.cat == sIDENTIF) {
        parse_decls(CAT_VAR_LOCAL);
    }
    int total_alocado = offset_local - offset_antes;
    if (total_alocado > 0) {
        gera_instr_mepa(NULL, "AMEM", int_to_str(total_alocado), NULL);
    }
}

static SimboloTipo parse_type(int *extra_out) {
    SimboloTipo tipo;

    if (current_tok.cat == sINT) {
        tipo = TIPO_INT;
        advance();
    } else if (current_tok.cat == sLOGIC) {
        tipo = TIPO_BOOL;
        advance();
    } else if (current_tok.cat == sCHR) {
        tipo = TIPO_CHR;
        advance();
    } else {
        diag_error_syntax(current_tok.line, "sINT, sLOGIC ou sCHR", tokenCatNome(current_tok.cat));
        tipo = TIPO_INT;
    }

    if (extra_out != NULL) {
        *extra_out = 0;
    }

    if (current_tok.cat == sABRECOL) {
        int tamanho = 0;
        check(sABRECOL);
        if (current_tok.cat == sCTEINT) {
            tamanho = atoi(current_tok.lexeme);
        }
        check(sCTEINT);
        check(sFECHACOL);
        if (extra_out != NULL) {
            *extra_out = tamanho;
        }
    }

    return tipo;
}

static void parse_id(void) {
    check(sIDENTIF);
}

static int parse_param(char nomes[][32], SimboloTipo tipos[], int extras[]) {
    int count = 0;
    param_refs[count] = current_tok.cat == sREF;

    if (current_tok.cat == sREF) {
        check(sREF);
    }
    strncpy(nomes[count], current_tok.lexeme, 31);
    nomes[count][31] = '\0';
    parse_id();
    check(sDOISPONTOS);
    tipos[count] = parse_type(&extras[count]);
    count++;

    while (current_tok.cat == sVIRG) {
        check(sVIRG);
        if (count >= MAX_PARAMS) diag_error(current_tok.line, "excesso de parametros");
        param_refs[count] = current_tok.cat == sREF;
        if (current_tok.cat == sREF) {
            check(sREF);
        }
        strncpy(nomes[count], current_tok.lexeme, 31);
        nomes[count][31] = '\0';
        parse_id();
        check(sDOISPONTOS);
        tipos[count] = parse_type(&extras[count]);
        count++;
    }

    return count;
}

static void parse_func(void) {
    char nome[32];
    SimboloTipo tipoRet;
    int nparams = 0;
    char rotulo_sub[16];
    char p_nomes[MAX_PARAMS][32];
    SimboloTipo p_tipos[MAX_PARAMS];
    int p_extras[MAX_PARAMS];

    if (saw_main) {
        diag_error(current_tok.line, "main deve ser a ultima sub-rotina do programa");
    }

    check(sFUNC);
    strncpy(nome, current_tok.lexeme, sizeof(nome) - 1);
    nome[sizeof(nome) - 1] = '\0';
    parse_id();

    strncpy(rotulo_sub, novo_rotulo(), sizeof(rotulo_sub));

    if (!symtab_insert(nome, CAT_FUNCAO, TIPO_VOID, 0, 0)) {
        diag_error(current_tok.line, "identificador de funcao duplicado");
    }
    Simbolo *s_func = symtab_lookup(nome);
    if (s_func) {
        strncpy(s_func->rotulo, rotulo_sub, sizeof(s_func->rotulo) - 1);
    }

    symtab_enter_scope(nome);
    strncpy(escopo_atual_nome, nome, sizeof(escopo_atual_nome) - 1);
    offset_local = 0;

    check(sABREPAR);
    if (current_tok.cat != sFECHAPAR) {
        nparams = parse_param(p_nomes, p_tipos, p_extras);
    }
    check(sFECHAPAR);
    check(sDOISPONTOS);
    tipoRet = parse_type(NULL);
    if (s_func) {
        s_func->tipo = tipoRet;
    }

    /* Insere parâmetros com deslocamentos MEPA corretos:
       Primeiro parâmetro em -(nparams + 2), último em -3 */
    for (int i = 0; i < nparams; i++) {
    int desloc = -(nparams + 4 - i);
        if (!symtab_insert(p_nomes[i], CAT_PARAM, p_tipos[i], desloc, p_extras[i])) {
            diag_error_syntax(current_tok.line, "identificador de parametro duplicado", p_nomes[i]);
        }
        symtab_lookup(p_nomes[i])->eh_referencia = param_refs[i] || p_extras[i] > 0;
        s_func->params[i].tipo = p_tipos[i];
        s_func->params[i].eh_referencia = param_refs[i] || p_extras[i] > 0;
    }
    s_func->extra = nparams;
    retorno_tipo = tipoRet;
    houve_retorno = false;
    strcpy(retorno_rotulo, novo_rotulo());

    func_atual_nparams = nparams;
    dentro_de_funcao = true;

    gera_instr_mepa(rotulo_sub, "ENPR", int_to_str(1), NULL);

    if (current_tok.cat == sLOCVARS) {
        parse_lvars();
    }
    parse_block();
    if (!houve_retorno) diag_error(current_tok.line, "funcao sem retorno");
    gera_instr_mepa(retorno_rotulo, "NADA", NULL, NULL);

    if (offset_local > 0) {
        gera_instr_mepa(NULL, "DMEM", int_to_str(offset_local), NULL);
    }
    
    // Desempilha a quantidade de parâmetros da função
    gera_instr_mepa(NULL, "RTPR", int_to_str(func_atual_nparams), NULL);

    dentro_de_funcao = false;
    symtab_leave_scope();
    symtab_update_signature(nome, tipoRet, nparams);
}

static void parse_proc(void) {
    char nome[32];
    int nparams = 0;
    char rotulo_sub[16];
    char p_nomes[MAX_PARAMS][32];
    SimboloTipo p_tipos[MAX_PARAMS];
    int p_extras[MAX_PARAMS];

    check(sPROC);
    strncpy(nome, current_tok.lexeme, sizeof(nome) - 1);
    nome[sizeof(nome) - 1] = '\0';
    parse_id();

    int eh_main = (strcmp(nome, "main") == 0);

    if (eh_main) {
        if (saw_main) diag_error(current_tok.line, "procedimento main duplicado");
        saw_main = 1;
        strncpy(rotulo_sub, "L0", sizeof(rotulo_sub));
    } else {
        if (saw_main) {
            diag_error(current_tok.line, "main deve ser a ultima sub-rotina do programa");
        }
        strncpy(rotulo_sub, novo_rotulo(), sizeof(rotulo_sub));
    }

    if (!symtab_insert(nome, CAT_PROCEDIMENTO, TIPO_VOID, 0, 0)) {
        diag_error(current_tok.line, "identificador de procedimento duplicado");
    }
    Simbolo *s_proc = symtab_lookup(nome);
    if (s_proc) {
        strncpy(s_proc->rotulo, rotulo_sub, sizeof(s_proc->rotulo) - 1);
    }

    symtab_enter_scope(nome);
    strncpy(escopo_atual_nome, nome, sizeof(escopo_atual_nome) - 1);
    offset_local = 0;

    check(sABREPAR);
    if (current_tok.cat != sFECHAPAR) {
        nparams = parse_param(p_nomes, p_tipos, p_extras);
    }
    if (eh_main && nparams != 0) {
        diag_error(current_tok.line, "proc main nao pode receber parametros");
    }
    check(sFECHAPAR);

    for (int i = 0; i < nparams; i++) {
        int desloc = -(nparams + 4 - i);
        if (!symtab_insert(p_nomes[i], CAT_PARAM, p_tipos[i], desloc, p_extras[i])) {
            diag_error_syntax(current_tok.line, "identificador de parametro duplicado", p_nomes[i]);
        }
        symtab_lookup(p_nomes[i])->eh_referencia = param_refs[i] || p_extras[i] > 0;
        s_proc->params[i].tipo = p_tipos[i];
        s_proc->params[i].eh_referencia = param_refs[i] || p_extras[i] > 0;
    }
    s_proc->extra = nparams;

    gera_instr_mepa(rotulo_sub, "ENPR", int_to_str(1), NULL);

    if (current_tok.cat == sLOCVARS) {
        parse_lvars();
    }
    parse_block();

    if (eh_main) {
        if (offset_local > 0) {
            gera_instr_mepa(NULL, "DMEM", int_to_str(offset_local), NULL);
        }
    } else {
        if (offset_local > 0) {
            gera_instr_mepa(NULL, "DMEM", int_to_str(offset_local), NULL);
        }
        gera_instr_mepa(NULL, "RTPR", int_to_str(nparams), NULL);
    }

    symtab_leave_scope();
    symtab_update_signature(nome, TIPO_VOID, nparams);
}

static void parse_subs(void) {
    if (current_tok.cat == sFUNC) {
        parse_func();
    } else if (current_tok.cat == sPROC) {
        parse_proc();
    } else {
        diag_error_syntax(current_tok.line, "sFUNC ou sPROC", tokenCatNome(current_tok.cat));
    }
}

static void parse_block(void) {
    check(sSTART);
    parse_cmd_list();
    check(sEND);
}

static void parse_cmd_list(void) {
    while (current_tok.cat == sECHO   || current_tok.cat == sGET    ||
           current_tok.cat == sCASE   || current_tok.cat == sCHOOSE ||
           current_tok.cat == sFOR    || current_tok.cat == sWHILE  ||
           current_tok.cat == sREPEAT || current_tok.cat == sRETURN ||
           current_tok.cat == sIDENTIF) {
        parse_cmd();
        check(sPONTOVIRG);
    }
}

static void parse_vetr(void) {
    char nome[32];
    strncpy(nome, current_tok.lexeme, sizeof(nome) - 1);
    nome[sizeof(nome) - 1] = '\0';
    Simbolo *s = parse_use_id();
    if (s && s->extra <= 0) {
        diag_error(current_tok.line, "identificador nao e um vetor");
    }
    gera_instr_mepa(NULL, s->eh_referencia ? "CRVL" : "CREN", int_to_str(s->level), int_to_str(s->offset));
    check(sABRECOL);
    if (current_tok.cat == sCTEINT) {
        gera_instr_mepa(NULL, "CRCT", current_tok.lexeme, NULL);
        advance();
    } else if (current_tok.cat == sIDENTIF) {
        Simbolo *idx = parse_use_id();
        if (idx) {
            gera_instr_mepa(NULL, "CRVL", int_to_str(idx->level), int_to_str(idx->offset));
        }
    } else {
        diag_error_syntax(current_tok.line, "sCTEINT ou sIDENTIF", tokenCatNome(current_tok.cat));
    }
    check(sFECHACOL);
    gera_instr_mepa(NULL, "INDX", "1", NULL);
}

static void parse_call(void) {
    char nome[32];
    int line = current_tok.line;
    strncpy(nome, current_tok.lexeme, sizeof(nome) - 1);
    nome[sizeof(nome) - 1] = '\0';
    parse_id();

    Simbolo *s = symtab_lookup(nome);
    if (!s || (s->cat != CAT_FUNCAO && s->cat != CAT_PROCEDIMENTO)) {
        char msg[160];
        snprintf(msg, sizeof(msg), "sub-rotina '%s' nao declarada", nome);
        diag_error(line, msg);
    }

    if (s && s->cat == CAT_FUNCAO) {
        gera_instr_mepa(NULL, "AMEM", "1", NULL);
    }

    check(sABREPAR);
    int arg_count = 0;
    while (current_tok.cat != sFECHAPAR) {
        if (arg_count >= s->extra) diag_error(line, "quantidade incorreta de argumentos");
        if (s->params[arg_count].eh_referencia) {
            if (current_tok.cat != sIDENTIF) diag_error(line, "referencia requer variavel");
            Simbolo *arg = parse_use_id();
            if (arg->cat != CAT_PARAM && arg->cat != CAT_VAR_LOCAL && arg->cat != CAT_VAR_GLOBAL)
                diag_error(line, "referencia requer variavel");
            if (arg->tipo != s->params[arg_count].tipo) diag_error(line, "tipo de argumento incompativel");
            gera_instr_mepa(NULL, arg->eh_referencia ? "CRVL" : "CREN", int_to_str(arg->level), int_to_str(arg->offset));
        } else {
            SimboloTipo t = parse_expr();
            if (t != s->params[arg_count].tipo) diag_error(line, "tipo de argumento incompativel");
        }
        arg_count++;
        if (current_tok.cat != sVIRG) break;
        check(sVIRG);
        if (current_tok.cat == sFECHAPAR) diag_error(line, "argumento ausente");
    }
    check(sFECHAPAR);
    if (s->extra != arg_count) diag_error(line, "quantidade incorreta de argumentos");
    
    if (s && strlen(s->rotulo) > 0) {
        gera_instr_mepa(NULL, "CHPR", s->rotulo, int_to_str(1));
    }
}

static void parse_atr(void) {
    char nome[32];
    strncpy(nome, current_tok.lexeme, sizeof(nome) - 1);
    nome[sizeof(nome) - 1] = '\0';
    int line = current_tok.line;

    if (peek().cat == sABRECOL) {
        parse_vetr();
        check(sATRIB);
        parse_elem();
        gera_instr_mepa(NULL, "ARVM", "1", NULL);
    } else {
        Simbolo *s = parse_use_id();
        check(sATRIB);
        if (s->tipo == TIPO_CHR && s->tam_vetor > 0 && current_tok.cat == sSTRING) {
            int len = (int)strlen(current_tok.lexeme);
            if (len > s->tam_vetor) diag_error(line, "string excede capacidade");
            gera_instr_mepa(NULL, s->eh_referencia ? "CRVL" : "CREN", int_to_str(s->level), int_to_str(s->offset));
            gera_instr_mepa(NULL, "CRCT", int_to_str(len), NULL);
            for (int j = 0; j < s->tam_vetor; j++)
                gera_instr_mepa(NULL, "CRCT", int_to_str(j < len ? (unsigned char)current_tok.lexeme[j] : 0), NULL);
            gera_instr_mepa(NULL, "ARVM", int_to_str(s->tam_vetor + 1), NULL);
            advance();
            return;
        }
        SimboloTipo t_expr = parse_elem();

        if (s && s->tipo != t_expr && t_expr != TIPO_INDEF) {
            diag_error(line, "incompatibilidade de tipos na atribuicao");
        }

        if (s) {
            gera_instr_mepa(NULL, s->eh_referencia ? "ARMI" : "ARMZ", int_to_str(s->level), int_to_str(s->offset));
        }
    }
}

static void parse_ret(void) {
    check(sRETURN);
    SimboloTipo t = parse_elem();
    if (!dentro_de_funcao) diag_error(current_tok.line, "return fora de funcao");
    if (t != retorno_tipo) diag_error(current_tok.line, "tipo de retorno incompativel");
    houve_retorno = true;
    if (dentro_de_funcao) {
        int endereco_ret = -(func_atual_nparams + 5);
        gera_instr_mepa(NULL, "ARMZ", "1", int_to_str(endereco_ret));
        gera_instr_mepa(NULL, "DSVS", retorno_rotulo, NULL);
    }
}

static void parse_echo(void) {
    check(sECHO);
    check(sABREPAR);

    while (1) {
        if (current_tok.cat == sSTRING) {
            char str_formatada[256];
            if (current_tok.lexeme[0] == '"') {
                strncpy(str_formatada, current_tok.lexeme, sizeof(str_formatada) - 1);
            } else {
                snprintf(str_formatada, sizeof(str_formatada), "\"%s\"", current_tok.lexeme);
            }
            str_formatada[sizeof(str_formatada) - 1] = '\0';

            gera_instr_mepa(NULL, "CRST", str_formatada, NULL);
            int len = (int)strlen(str_formatada) - 2;
            if (len < 0) len = 0;
            gera_instr_mepa(NULL, "IMPS", int_to_str(len + 1), NULL);
            advance();
        } else if (current_tok.cat == sIDENTIF && symtab_lookup(current_tok.lexeme) &&
                   symtab_lookup(current_tok.lexeme)->tipo == TIPO_CHR &&
                   symtab_lookup(current_tok.lexeme)->tam_vetor > 0 && peek().cat != sABRECOL) {
            Simbolo *str = parse_use_id();
            gera_instr_mepa(NULL, str->eh_referencia ? "CRVL" : "CREN", int_to_str(str->level), int_to_str(str->offset));
            gera_instr_mepa(NULL, "CRVM", int_to_str(str->tam_vetor + 1), NULL);
            gera_instr_mepa(NULL, "IMPS", int_to_str(str->tam_vetor + 1), NULL);
        } else {
            SimboloTipo t = parse_elem();
            if (t == TIPO_CHR) {
                gera_instr_mepa(NULL, "IMPC", NULL, NULL);
            } else {
                gera_instr_mepa(NULL, "IMPR", NULL, NULL);
            }
        }

        if (current_tok.cat == sVIRG) {
            check(sVIRG);
        } else {
            break;
        }
    }
    check(sFECHAPAR);
}

static void parse_get(void) {
    check(sGET);
    check(sABREPAR);
    char nome[32];
    strncpy(nome, current_tok.lexeme, sizeof(nome) - 1);
    nome[sizeof(nome) - 1] = '\0';

    if (peek().cat == sABRECOL) {
        parse_vetr();
        gera_instr_mepa(NULL, "LEIT", NULL, NULL);
        gera_instr_mepa(NULL, "ARVM", "1", NULL);
    } else {
        Simbolo *s = parse_use_id();
        gera_instr_mepa(NULL, "LEIT", NULL, NULL);
        if (s) {
            gera_instr_mepa(NULL, s->eh_referencia ? "ARMI" : "ARMZ", int_to_str(s->level), int_to_str(s->offset));
        }
    }
    check(sFECHAPAR);
}

static void parse_case(void) {
    check(sCASE);
    check(sABREPAR);
    SimboloTipo t = parse_expr();
    if (t != TIPO_BOOL) {
        diag_error(current_tok.line, "expressao de case deve ser logic");
    }
    check(sFECHAPAR);

    char rotulo_senao[16], rotulo_fim[16];
    strncpy(rotulo_senao, novo_rotulo(), sizeof(rotulo_senao));
    strncpy(rotulo_fim, novo_rotulo(), sizeof(rotulo_fim));

    gera_instr_mepa(NULL, "DSVF", rotulo_senao, NULL);

    parse_cmd_list();

    if (current_tok.cat == sOTHERWISE) {
        gera_instr_mepa(NULL, "DSVS", rotulo_fim, NULL);
        gera_instr_mepa(rotulo_senao, "NADA", NULL, NULL);
        check(sOTHERWISE);
        parse_cmd_list();
        gera_instr_mepa(rotulo_fim, "NADA", NULL, NULL);
    } else {
        gera_instr_mepa(rotulo_senao, "NADA", NULL, NULL);
    }
    check(sEND);
}

static void parse_mvlrs(void) {
    check(sCTEINT);
    while (current_tok.cat == sVIRG) {
        check(sVIRG);
        check(sCTEINT);
    }
}

static void parse_mtch(const char *rotulo_fim) {
    char rot_prox[16];
    strncpy(rot_prox, novo_rotulo(), sizeof(rot_prox));

    check(sMATCH);
    int slot = choose_offsets[choose_depth - 1];
    int first = 1;
    do {
        if (!first) check(sVIRG);
        gera_instr_mepa(NULL, "CRVL", "1", int_to_str(slot));
        gera_instr_mepa(NULL, "CRCT", current_tok.lexeme, NULL);
        check(sCTEINT);
        gera_instr_mepa(NULL, "CMIG", NULL, NULL);
        if (!first) gera_instr_mepa(NULL, "DISJ", NULL, NULL);
        first = 0;
    } while (current_tok.cat == sVIRG);
    gera_instr_mepa(NULL, "DSVF", rot_prox, NULL);

    check(sIMPLIC);
    parse_cmd();
    check(sPONTOVIRG);

    gera_instr_mepa(NULL, "DSVS", rotulo_fim, NULL);
    gera_instr_mepa(rot_prox, "NADA", NULL, NULL);
}

static void parse_othr(void) {
    check(sOTHERS);
    check(sIMPLIC);
    parse_cmd();
    check(sPONTOVIRG);
}

static void parse_mlst(const char *rotulo_fim) {
    parse_mtch(rotulo_fim);
    while (current_tok.cat == sMATCH) {
        parse_mtch(rotulo_fim);
    }
    if (current_tok.cat == sOTHERS) {
        parse_othr();
    }
}

static void parse_chse(void) {
    check(sCHOOSE);
    check(sABREPAR);
    if (choose_depth >= 64) diag_error(current_tok.line, "excesso de choose aninhado");
    choose_offsets[choose_depth++] = offset_local++;
    gera_instr_mepa(NULL, "AMEM", "1", NULL);
    if (parse_expr() != TIPO_INT) diag_error(current_tok.line, "choose requer int");
    gera_instr_mepa(NULL, "ARMZ", "1", int_to_str(offset_local - 1));
    check(sFECHAPAR);

    char rotulo_fim[16];
    strncpy(rotulo_fim, novo_rotulo(), sizeof(rotulo_fim));

    parse_mlst(rotulo_fim);
    check(sEND);

    gera_instr_mepa(rotulo_fim, "NADA", NULL, NULL);
    gera_instr_mepa(NULL, "DMEM", "1", NULL);
    offset_local--;
    choose_depth--;
}

static void parse_for(void) {
    check(sFOR);
    Simbolo *s = parse_use_id();
    check(sFROM);

    char rot_ini[16], rot_fim[16];
    strncpy(rot_ini, novo_rotulo(), sizeof(rot_ini));
    strncpy(rot_fim, novo_rotulo(), sizeof(rot_fim));

    if (current_tok.cat == sCTEINT) {
        gera_instr_mepa(NULL, "CRCT", current_tok.lexeme, NULL);
        advance();
    } else {
        Simbolo *s_ini = parse_use_id();
        if (s_ini) gera_instr_mepa(NULL, "CRVL", int_to_str(s_ini->level), int_to_str(s_ini->offset));
    }
    if (s) {
        gera_instr_mepa(NULL, s->eh_referencia ? "ARMI" : "ARMZ", int_to_str(s->level), int_to_str(s->offset));
    }

    gera_instr_mepa(rot_ini, "NADA", NULL, NULL);

    check(sTO);
    if (s) {
        gera_instr_mepa(NULL, s->eh_referencia ? "CRVI" : "CRVL", int_to_str(s->level), int_to_str(s->offset));
    }
    if (current_tok.cat == sCTEINT) {
        gera_instr_mepa(NULL, "CRCT", current_tok.lexeme, NULL);
        advance();
    } else {
        Simbolo *s_fim = parse_use_id();
        if (s_fim) gera_instr_mepa(NULL, "CRVL", int_to_str(s_fim->level), int_to_str(s_fim->offset));
    }
    gera_instr_mepa(NULL, "CMEG", NULL, NULL);
    gera_instr_mepa(NULL, "DSVF", rot_fim, NULL);

    int passo = 1;
    if (current_tok.cat == sBY) {
        check(sBY);
        passo = atoi(current_tok.lexeme);
        check(sCTEINT);
    }
    check(sDO);
    parse_cmd_list();
    check(sEND);

    if (s) {
        gera_instr_mepa(NULL, s->eh_referencia ? "CRVI" : "CRVL", int_to_str(s->level), int_to_str(s->offset));
        gera_instr_mepa(NULL, "CRCT", int_to_str(passo), NULL);
        gera_instr_mepa(NULL, "SOMA", NULL, NULL);
        gera_instr_mepa(NULL, s->eh_referencia ? "ARMI" : "ARMZ", int_to_str(s->level), int_to_str(s->offset));
    }
    gera_instr_mepa(NULL, "DSVS", rot_ini, NULL);
    gera_instr_mepa(rot_fim, "NADA", NULL, NULL);
}

static void parse_whle(void) {
    check(sWHILE);
    check(sABREPAR);

    char rot_ini[16], rot_fim[16];
    strncpy(rot_ini, novo_rotulo(), sizeof(rot_ini));
    strncpy(rot_fim, novo_rotulo(), sizeof(rot_fim));

    gera_instr_mepa(rot_ini, "NADA", NULL, NULL);

    SimboloTipo t = parse_expr();
    if (t != TIPO_BOOL) {
        diag_error(current_tok.line, "condicao do while deve ser logic");
    }
    check(sFECHAPAR);
    check(sDO);

    gera_instr_mepa(NULL, "DSVF", rot_fim, NULL);

    parse_cmd_list();
    check(sEND);

    gera_instr_mepa(NULL, "DSVS", rot_ini, NULL);
    gera_instr_mepa(rot_fim, "NADA", NULL, NULL);
}

static void parse_rept(void) {
    check(sREPEAT);

    char rot_ini[16];
    strncpy(rot_ini, novo_rotulo(), sizeof(rot_ini));

    gera_instr_mepa(rot_ini, "NADA", NULL, NULL);

    parse_cmd_list();
    check(sUNTIL);
    check(sABREPAR);
    SimboloTipo t = parse_expr();
    if (t != TIPO_BOOL) {
        diag_error(current_tok.line, "condicao do repeat deve ser logic");
    }
    check(sFECHAPAR);

    gera_instr_mepa(NULL, "DSVF", rot_ini, NULL);
}

static void parse_cmd(void) {
    switch (current_tok.cat) {
        case sECHO:   parse_echo(); break;
        case sGET:    parse_get(); break;
        case sCASE:   parse_case(); break;
        case sCHOOSE: parse_chse(); break;
        case sFOR:    parse_for(); break;
        case sWHILE:  parse_whle(); break;
        case sREPEAT: parse_rept(); break;
        case sRETURN: parse_ret(); break;
        case sIDENTIF:
            if (peek().cat == sABREPAR) {
                parse_call();
            } else {
                parse_atr();
            }
            break;
        default:
            diag_error_syntax(current_tok.line, "comando valido", tokenCatNome(current_tok.cat));
            break;
    }
}

static SimboloTipo parse_atom(void) {
    if (current_tok.cat == sSTRING) {
        char str_formatada[256];
        if (current_tok.lexeme[0] == '"') {
            strncpy(str_formatada, current_tok.lexeme, sizeof(str_formatada) - 1);
        } else {
            snprintf(str_formatada, sizeof(str_formatada), "\"%s\"", current_tok.lexeme);
        }
        str_formatada[sizeof(str_formatada) - 1] = '\0';
        gera_instr_mepa(NULL, "CRST", str_formatada, NULL);
        advance();
        return TIPO_CHR;
    }
    if (current_tok.cat == sCTEINT) {
        gera_instr_mepa(NULL, "CRCT", current_tok.lexeme, NULL);
        advance();
        return TIPO_INT;
    }
    if (current_tok.cat == sCTECHAR) {
        char val[16];
        snprintf(val, sizeof(val), "%d", (unsigned char)current_tok.lexeme[0]);
        gera_instr_mepa(NULL, "CRCT", val, NULL);
        advance();
        return TIPO_CHR;
    }

    if (current_tok.cat == sIDENTIF) {
        TokenCategoria prox = peek().cat;
        if (prox == sABRECOL) {
            parse_vetr();
            gera_instr_mepa(NULL, "CRVM", "1", NULL);
            return TIPO_INT;
        } else if (prox == sABREPAR) {
            char nome[32];
            strncpy(nome, current_tok.lexeme, sizeof(nome) - 1);
            nome[sizeof(nome) - 1] = '\0';
            Simbolo *s = symtab_lookup(nome);
            parse_call();
            return s ? s->tipo : TIPO_INDEF;
        } else {
            Simbolo *s = parse_use_id();
            if (s) {
                gera_instr_mepa(NULL, s->eh_referencia ? "CRVI" : "CRVL", int_to_str(s->level), int_to_str(s->offset));
                return s->tipo;
            }
            return TIPO_INDEF;
        }
    }

    diag_error_syntax(current_tok.line, "elemento valido", tokenCatNome(current_tok.cat));
    return TIPO_INDEF;
}

static SimboloTipo parse_elem(void) {
    return parse_expr();
}

static SimboloTipo parse_factor(void) {
    if (current_tok.cat == sNEG) {
        advance();
        SimboloTipo t = parse_factor();
        if (t != TIPO_BOOL) diag_error(current_tok.line, "operador '~' requer tipo logic");
        gera_instr_mepa(NULL, "NEGA", NULL, NULL);
        return TIPO_BOOL;
    } else if (current_tok.cat == sSUBRAT) {
        advance();
        SimboloTipo t = parse_factor();
        if (t != TIPO_INT) diag_error(current_tok.line, "menos unario requer tipo int");
        gera_instr_mepa(NULL, "INVR", NULL, NULL);
        return TIPO_INT;
    } else if (current_tok.cat == sABREPAR) {
        check(sABREPAR);
        SimboloTipo t = parse_expr();
        check(sFECHAPAR);
        return t;
    } else {
        return parse_atom();
    }
}

static SimboloTipo parse_earip(void) {
    SimboloTipo t1 = parse_factor();
    while (current_tok.cat == sMULT || current_tok.cat == sDIV) {
        TokenCategoria op = current_tok.cat;
        advance();
        SimboloTipo t2 = parse_factor();
        if (t1 != TIPO_INT || t2 != TIPO_INT) {
            diag_error(current_tok.line, "operacao multiplicativa requer int");
        }
        if (op == sMULT) {
            gera_instr_mepa(NULL, "MULT", NULL, NULL);
        } else {
            gera_instr_mepa(NULL, "DIVI", NULL, NULL);
        }
        t1 = TIPO_INT;
    }
    return t1;
}

static SimboloTipo parse_earit(void) {
    SimboloTipo t1 = parse_earip();
    while (current_tok.cat == sSOMA || current_tok.cat == sSUBRAT) {
        TokenCategoria op = current_tok.cat;
        advance();
        SimboloTipo t2 = parse_earip();
        if (t1 != TIPO_INT || t2 != TIPO_INT) {
            diag_error(current_tok.line, "operacao aditiva requer int");
        }
        if (op == sSOMA) {
            gera_instr_mepa(NULL, "SOMA", NULL, NULL);
        } else {
            gera_instr_mepa(NULL, "SUBT", NULL, NULL);
        }
        t1 = TIPO_INT;
    }
    return t1;
}

static SimboloTipo parse_erlac(void) {
    SimboloTipo t1 = parse_earit();
    while (current_tok.cat == sMAIOR || current_tok.cat == sMAIORIGUAL ||
           current_tok.cat == sIGUAL || current_tok.cat == sMENOR ||
           current_tok.cat == sMENORIGUAL || current_tok.cat == sDIFERENTE) {
        TokenCategoria op = current_tok.cat;
        advance();
        SimboloTipo t2 = parse_earit();
        if (t1 != t2) {
            diag_error(current_tok.line, "comparacao requer tipos compativeis");
        }
        switch (op) {
            case sMAIOR:      gera_instr_mepa(NULL, "CMMA", NULL, NULL); break;
            case sMAIORIGUAL: gera_instr_mepa(NULL, "CMAG", NULL, NULL); break;
            case sMENOR:      gera_instr_mepa(NULL, "CMME", NULL, NULL); break;
            case sMENORIGUAL: gera_instr_mepa(NULL, "CMEG", NULL, NULL); break;
            case sIGUAL:      gera_instr_mepa(NULL, "CMIG", NULL, NULL); break;
            case sDIFERENTE:  gera_instr_mepa(NULL, "CMDG", NULL, NULL); break;
            default: break;
        }
        t1 = TIPO_BOOL;
    }
    return t1;
}

static SimboloTipo parse_elogc(void) {
    SimboloTipo t1 = parse_erlac();
    while (current_tok.cat == sAND) {
        check(sAND);
        SimboloTipo t2 = parse_erlac();
        if (t1 != TIPO_BOOL || t2 != TIPO_BOOL) {
            diag_error(current_tok.line, "operador '&' requer logic");
        }
        gera_instr_mepa(NULL, "CONJ", NULL, NULL);
        t1 = TIPO_BOOL;
    }
    return t1;
}

static SimboloTipo parse_expr(void) {
    SimboloTipo t1 = parse_elogc();
    while (current_tok.cat == sOR) {
        check(sOR);
        SimboloTipo t2 = parse_elogc();
        if (t1 != TIPO_BOOL || t2 != TIPO_BOOL) {
            diag_error(current_tok.line, "operador '|' requer logic");
        }
        gera_instr_mepa(NULL, "DISJ", NULL, NULL);
        t1 = TIPO_BOOL;
    }
    return t1;
}

void parse_program(void) {
    advance();
    saw_main = 0;
    offset_global = 0;

    gera_instr_mepa(NULL, "INPP", NULL, NULL);

    symtab_enter_scope("global");

    if (current_tok.cat == sGLOBVARS) {
        parse_gvars();
    }

    gera_instr_mepa(NULL, "DSVS", "L0", NULL);

    while (current_tok.cat == sFUNC || current_tok.cat == sPROC) {
        parse_subs();
    }

    if (!saw_main) {
        diag_error(current_tok.line, "programa deve conter o procedimento proc main()");
    }

    if (offset_global > 0) {
        gera_instr_mepa(NULL, "DMEM", int_to_str(offset_global), NULL);
    }
    gera_instr_mepa(NULL, "PARA", NULL, NULL);
    gera_instr_mepa(NULL, "FIM", NULL, NULL);

    symtab_leave_scope();
    
    check(TOKEN_EOF);
}