#define PARSER_CAPACITY 100

void infix_to_postfix(const char *infix, int *postfix);

int postfix_to_eval(const int *postfix);
