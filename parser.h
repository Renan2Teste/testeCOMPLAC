#ifndef PARSER_H
#define PARSER_H

/* Inicia a analise sintatica descendente recursiva sobre o ficheiro carregado no lexer
   e gera o codigo intermediario correspondente para a MEPA. */
void parse_program(void);

#endif /* PARSER_H */