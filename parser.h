#define PARSER_CAPACITY 100

int infix_to_postfix(const char *infix, int *postfix);

int postfix_to_eval(const int *postfix, int count);
