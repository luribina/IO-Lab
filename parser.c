#include <linux/ctype.h>
#include <linux/types.h>

#include "parser.h"

typedef enum
{
    NUMBER,
    OPERATION
} postfix_info;
postfix_info info[PARSER_CAPACITY];

static int stack[PARSER_CAPACITY];

int top = -1;

static void push(int el)
{
    stack[++top] = el;
}

static int pop(void)
{
    return stack[top--];
}

static bool empty(void)
{
    return top < 0;
}

static bool is_number(int index)
{
    return info[index] == NUMBER;
}

static int priority(char el)
{
    switch (el)
    {
    case '(':
        return 0;
    case '+':
    case '-':
        return 1;
    case '*':
    case '/':
        return 2;
    default:
        return 0;
    }
}

void infix_to_postfix(const char *infix, int *postfix)
{
    int i = 0;
    int p = 0;
    int temp_int = 0;
    char el;
    while ((el = infix[i++]) != '\0')
    {
        if (isspace(el))
        {
            continue;
        }
        if (isdigit(el))
        {
            temp_int = el - '0';
            while ((el = infix[i++]) != '\0')
            {
                if (isdigit(el))
                {
                    temp_int = temp_int * 10 + (el - '0');
                }
                else
                {
                    break;
                }
            }
            info[p] = NUMBER;
            postfix[p++] = temp_int;
            i--;
        }
        else if (el == '(')
        {
            push(el);
        }
        else if (el == ')')
        {
            while (stack[top] != '(')
            {
                info[p] = OPERATION;
                postfix[p++] = pop();
            }
            pop();
        }
        else
        {
            while (!empty() && priority(stack[top]) >= priority(el))
            {
                info[p] = OPERATION;
                postfix[p++] = pop();
            }
            push(el);
        }
    }
    while (!empty())
    {
        info[p] = OPERATION;
        postfix[p++] = pop();
    }
    postfix[p] = '\0';
}

int postfix_to_eval(const int *postfix)
{
    int el;
    int i = 0, op1, op2;
    while ((el = postfix[i++]) != '\0')
    {
        if (is_number(i - 1))
        {
            push(el);
        }
        else
        {
            op2 = pop();
            op1 = pop();
            switch (el)
            {
            case '+':
                push(op1 + op2);
                break;
            case '-':
                push(op1 - op2);
                break;
            case '*':
                push(op1 * op2);
                break;
            case '/':
                push(op1 / op2);
                break;
            }
        }
    }
    return pop();
}
