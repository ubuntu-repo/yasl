#include <interpreter/YASL_Object/YASL_Object.h>
#include <interpreter/YASL_string/YASL_string.h>
#include <bytebuffer/bytebuffer.h>
#include <metadata.h>
#include <compiler/parser/parser.h>
#include <yasl_error.h>
#include <yasl_include.h>
#include "compiler.h"
#define break_checkpoint(compiler)    ((compiler)->checkpoints[(compiler)->checkpoints_count-1])
#define continue_checkpoint(compiler) ((compiler)->checkpoints[(compiler)->checkpoints_count-2])



struct Compiler *compiler_new(Parser *const parser) {
    struct Compiler *compiler = malloc(sizeof(struct Compiler));

    compiler->globals = env_new(NULL);
    compiler->params = NULL;

    compiler->offset = 0;
    compiler->strings = table_new();
    compiler->parser = parser;
    compiler->buffer = bb_new(16);
    compiler->header = bb_new(16);
    compiler->header->count = 16;
    compiler->status = YASL_SUCCESS;
    /*bb_add_byte(compiler->header, 'Y');
    bb_add_byte(compiler->header, 'Y');
    bb_add_byte(compiler->header, 'Y');
    bb_add_byte(compiler->header, 'Y');
    bb_add_byte(compiler->header, 'Y');
    bb_add_byte(compiler->header, 'Y');
    bb_add_byte(compiler->header, 'Y');
    bb_add_byte(compiler->header, 'Y');
    char magic_number[YASL_MAG_NUM_SIZE] = "YASL";
    magic_number[4] = YASL_COMPILER;
    magic_number[5] = YASL_MAJOR_VERSION;
    magic_number[6] = YASL_MINOR_VERSION;
    magic_number[7] = YASL_PATCH; */
    compiler->checkpoints_size = 4;
    compiler->checkpoints = malloc(sizeof(int64_t)*compiler->checkpoints_size);
    compiler->checkpoints_count = 0;
    compiler->code = bb_new(16);
    return compiler;
}

void compiler_tables_del(struct Compiler *compiler) {
    table_del_string_int(compiler->strings);
}

static void compiler_buffers_del(const struct Compiler *const compiler) {
    bb_del(compiler->buffer);
    bb_del(compiler->header);
    bb_del(compiler->code);
}

void compiler_del(struct Compiler *compiler) {
    compiler_tables_del(compiler);
    env_del(compiler->globals);
    env_del(compiler->params);
    parser_del(compiler->parser);
    compiler_buffers_del(compiler);
    free(compiler->checkpoints);
    free(compiler);
}

static void handle_error(struct Compiler *const compiler) {
    compiler->status = YASL_SYNTAX_ERROR;
}

static void enter_scope(struct Compiler *const compiler) {
    if (compiler->params != NULL) compiler->params = env_new(compiler->params);
    else compiler->globals = env_new(compiler->globals);
}

static void exit_scope(struct Compiler *const compiler) {
    if (compiler->params != NULL) {
        Env_t *tmp = compiler->params;
        compiler->params = compiler->params->parent;
        env_del_current_only(tmp);
    } else {
        Env_t *tmp = compiler->globals;
        compiler->globals = compiler->globals->parent;
        env_del_current_only(tmp);
    }
}

static inline void enter_conditional_false(struct Compiler *const compiler, int64_t *index) {
    bb_add_byte(compiler->buffer, BRF_8);
    *index = compiler->buffer->count;
    bb_intbytes8(compiler->buffer, 0);
}

static inline void exit_conditional_false(struct Compiler *const compiler, const int64_t *const index) {
    bb_rewrite_intbytes8(compiler->buffer, *index, compiler->buffer->count - *index - 8);
}

static void add_checkpoint(struct Compiler *const compiler, const int64_t cp) {
    if (compiler->checkpoints_count >= compiler->checkpoints_size)
        compiler->checkpoints = realloc(compiler->checkpoints, compiler->checkpoints_size *= 2);
    compiler->checkpoints[compiler->checkpoints_count++] = cp;
}

static void rm_checkpoint(struct Compiler *compiler) {
    compiler->checkpoints_count--;
}

static void visit(struct Compiler *const compiler, const struct Node *const node);

static void visit_Body(struct Compiler *const compiler, const struct Node *const node) {
    for (size_t i = 0; i < node->children_len; i++) {
        visit(compiler, node->children[i]);
    }
}

int is_const(int64_t value) {
    const uint64_t MASK = 0x8000000000000000;
    return (MASK & value) != 0;
}

static inline int64_t get_index(int64_t value) {
    return is_const(value) ? ~value : value;
}

static void load_var(struct Compiler *const compiler, char *name, int64_t name_len, size_t line) {
    if (env_contains(compiler->params, name, name_len)) {
        bb_add_byte(compiler->buffer, LLOAD_1);
        bb_add_byte(compiler->buffer, get_index(env_get(compiler->params, name, name_len))); // TODO: handle size
    } else if (env_contains(compiler->globals, name, name_len)){
        bb_add_byte(compiler->buffer, GLOAD_1);
        bb_add_byte(compiler->buffer, get_index(env_get(compiler->globals, name, name_len)));  // TODO: handle size
    } else {
        YASL_PRINT_ERROR_UNDECLARED_VAR(name, line);
        handle_error(compiler);
        return;
    }
}

static void store_var(struct Compiler *const compiler, char *name, size_t name_len, size_t line) {
    if (env_contains(compiler->params, name, name_len)) {
        int64_t index = env_get(compiler->params, name, name_len);
        if (is_const(index)) {
            YASL_PRINT_ERROR_CONSTANT(name, line);
            handle_error(compiler);
            return;
        }
        bb_add_byte(compiler->buffer, LSTORE_1);
        bb_add_byte(compiler->buffer, index); // TODO: handle size
    } else if (env_contains(compiler->globals, name, name_len)) {
        int64_t index = env_get(compiler->globals, name, name_len);
        if (is_const(index)) {
            YASL_PRINT_ERROR_CONSTANT(name, line);
            handle_error(compiler);
            return;
        }
        bb_add_byte(compiler->buffer, GSTORE_1);
        bb_add_byte(compiler->buffer, index);  // TODO: handle size
    } else {
        YASL_PRINT_ERROR_UNDECLARED_VAR(name, line);
        handle_error(compiler);
        return;
    }
}

static int contains_var_in_current_scope(const struct Compiler *const compiler, char *name, int64_t name_len) {
    return compiler->params ?
    env_contains_cur_scope(compiler->params, name, name_len) :
    env_contains_cur_scope(compiler->globals, name, name_len);
}

static int contains_var(const struct Compiler *const compiler, char *name, int64_t name_len) {
    return env_contains(compiler->globals, name, name_len) ||
           env_contains(compiler->params, name, name_len);
}

static void decl_var(struct Compiler *const compiler, char *name, int64_t name_len) {
    if (NULL != compiler->params) {
        env_decl_var(compiler->params, name, name_len);
    }
    else {
        env_decl_var(compiler->globals, name, name_len);
    }
}

static void make_const(struct Compiler * const compiler, char *name, int64_t name_len) {
    if (NULL != compiler->params) env_make_const(compiler->params, name, name_len);
    else env_make_const(compiler->globals, name, name_len);
}

unsigned char *compile(struct Compiler *const compiler) {
    struct Node *node;
    gettok(compiler->parser->lex);
    while (!peof(compiler->parser)) {
            if (peof(compiler->parser)) break;
            node = parse(compiler->parser);
            eattok(compiler->parser, T_SEMI);
            compiler->status |= compiler->parser->status;
            if (!compiler->parser->status) {
                visit(compiler, node);
                bb_append(compiler->code, compiler->buffer->bytes, compiler->buffer->count);
                compiler->buffer->count = 0;
            }/* else {
                node_del(node);
                return compiler->parser->status;
            }*/

            node_del(node);
    }

    if (compiler->status) return NULL;

    bb_rewrite_intbytes8(compiler->header, 0, compiler->header->count);
    bb_rewrite_intbytes8(compiler->header, 8, 0x00);   // TODO: put num globals here eventually.

    /*YASL_DEBUG_LOG("%s\n", "magic number");
    for (i = 0; i < 7; i++) {
        YASL_DEBUG_LOG("%02x\n", magic_number[i]);
    } */

    YASL_DEBUG_LOG("%s\n", "header");
    for (size_t i = 0; i < compiler->header->count; i++) {
        if (i % 16 == 15)
            YASL_DEBUG_LOG("%02x\n", compiler->header->bytes[i]);
        else
            YASL_DEBUG_LOG("%02x ", compiler->header->bytes[i]);
    }
    YASL_DEBUG_LOG("%s\n", "entry point");
    for (size_t i = 0; i < compiler->code->count; i++) {
        if (i % 16 == 15)
            YASL_DEBUG_LOG("%02x\n", compiler->code->bytes[i]);
        else
            YASL_DEBUG_LOG("%02x ", compiler->code->bytes[i]);
    }
    YASL_DEBUG_LOG("%02x\n", HALT);
    //FILE *fp = fopen(compiler->name, "wb");
    //if (!fp) exit(EXIT_FAILURE);

    fflush(stdout);
    //fwrite(magic_number, 1, YASL_MAG_NUM_SIZE, fp);
    //fwrite(compiler->header->bytes, 1, compiler->header->count, fp);
    //fwrite(compiler->code->bytes, 1, compiler->code->count, fp);
    unsigned char *bytecode = malloc(compiler->code->count + compiler->header->count + 1);    // NOT OWN
    memcpy(bytecode, compiler->header->bytes, compiler->header->count);
    memcpy(bytecode + compiler->header->count, compiler->code->bytes, compiler->code->count);
    bytecode[compiler->code->count + compiler->header->count] = HALT;
    //fputc(HALT, fp);
    //fclose(fp);
    return bytecode;
}

static void visit_ExprStmt(struct Compiler *const compiler, const struct Node *const node) {
    visit(compiler, ExprStmt_get_expr(node));
    bb_add_byte(compiler->buffer, POP);
}

static void visit_FunctionDecl(struct Compiler *const compiler, const struct Node *const node) {
    if (compiler->params != NULL) {
        YASL_PRINT_ERROR_SYNTAX("Illegal function declaration outside global scope, in line %zd.\n", node->line);
        handle_error(compiler);
        return;
    }

    // start logic for function, now that we are sure it's legal to do so, and have set up.

    // use offset to compute offsets for params, in other functions.
    compiler->offset = FnDecl_get_params(node)->children_len;
    //YASL_DEBUG_LOG("compiler->offset is: %d\n", compiler->offset);

    compiler->params = env_new(compiler->params);

    enter_scope(compiler);

    for (size_t i = 0; i < FnDecl_get_params(node)->children_len; i++) {
        decl_var(compiler, FnDecl_get_params(node)->children[i]->name, FnDecl_get_params(node)->children[i]->name_len);
    }

    bb_add_byte(compiler->buffer, FnDecl_get_params(node)->children_len);
    bb_add_byte(compiler->buffer, compiler->params->vars->count);
    visit_Body(compiler, node->children[1]);

    int64_t fn_val = compiler->header->count;
    bb_append(compiler->header, compiler->buffer->bytes, compiler->buffer->count);
    bb_add_byte(compiler->header, NCONST);
    bb_add_byte(compiler->header, RET);

    // zero buffer length
    compiler->buffer->count = 0;

    exit_scope(compiler);
    Env_t *tmp = compiler->params->parent;
    env_del_current_only(compiler->params);
    compiler->params = tmp;
    compiler->offset = 0;

    bb_add_byte(compiler->buffer, FCONST);
    bb_intbytes8(compiler->buffer, fn_val);
}

static void visit_Call(struct Compiler *const compiler, const struct Node *const node) {
    YASL_TRACE_LOG("Visit Call: %s\n", node->name);
    visit(compiler, node->children[1]);
    bb_add_byte(compiler->buffer, INIT_CALL);
    visit_Body(compiler, Call_get_params(node));
    bb_add_byte(compiler->buffer, CALL);
}

static void visit_Return(struct Compiler *const compiler, const struct Node *const node) {
    YASL_TRACE_LOG("Visit Return: %s\n", node->name);
    // recursive calls.
    /*
    if (node->nodetype == N_CALL && !strcmp(compiler->current_function, node->name)) {
        visit_Body(compiler, Return_get_expr(node));

        bb_add_byte(compiler->buffer, RCALL_8);
        bb_add_byte(compiler->buffer, Return_get_expr(node)->children_len);
        bb_intbytes8(compiler->buffer, rcht_search_string_int(compiler->functions, node->name, node->name_len)->value.ival);
        bb_add_byte(compiler->buffer, compiler->offset);

        return;
    }
    */

    // default case.
    visit(compiler, Return_get_expr(node));
    bb_add_byte(compiler->buffer, RET);
}

static void visit_Set(struct Compiler *const compiler, const struct Node *const node) {
    visit(compiler, Set_get_collection(node));
    visit(compiler, Set_get_key(node));
    visit(compiler, Set_get_value(node));
    bb_add_byte(compiler->buffer, SET);
}

static void visit_Get(struct Compiler *const compiler, const struct Node *const node) {
    visit(compiler, Get_get_collection(node));
    visit(compiler, Get_get_value(node));
    bb_add_byte(compiler->buffer, GET);
}

static void visit_Block(struct Compiler *const compiler, const struct Node *const node) {
    enter_scope(compiler);
    visit(compiler, node->children[0]);
    exit_scope(compiler);
}

static inline void branch_back(struct Compiler *const compiler, int64_t index) {
    bb_add_byte(compiler->buffer, BR_8);
    bb_intbytes8(compiler->buffer, index - compiler->buffer->count - 8);
}

static void visit_ListComp(struct Compiler *const compiler, const struct Node *const node) {
    enter_scope(compiler);

    visit(compiler, node->children[1]->children[1]);

    bb_add_byte(compiler->buffer, INITFOR);

    bb_add_byte(compiler->buffer, END);

    if (node->children[1]->nodetype == N_LETITER) {
        decl_var(compiler, node->children[1]->children[0]->name, node->children[1]->children[0]->name_len);
    } else if (!contains_var(compiler, node->children[1]->children[0]->name, node->children[1]->children[0]->name_len)){
        YASL_PRINT_ERROR_UNDECLARED_VAR(node->children[1]->children[0]->name, node->children[1]->children[0]->line);
        handle_error(compiler);
        exit_scope(compiler);
        return;
    }

    size_t index_start = compiler->buffer->count;

    bb_add_byte(compiler->buffer, ITER_1);

    int64_t index_second;
    enter_conditional_false(compiler, &index_second);

    store_var(compiler, node->children[1]->children[0]->name, node->children[1]->children[0]->name_len, node->line);

    if (node->children[2]) {
        int64_t index_third;
        visit(compiler, node->children[2]);
        enter_conditional_false(compiler, &index_third);

        visit(compiler, ListComp_get_expr(node));

        exit_conditional_false(compiler, &index_third);
    } else {
        visit(compiler, ListComp_get_expr(node));
    }

    // visit(compiler, ListComp_get_expr(node));

    branch_back(compiler, index_start);

    exit_conditional_false(compiler, &index_second);

    bb_add_byte(compiler->buffer, NEWLIST);

    bb_add_byte(compiler->buffer, ENDCOMP);
    exit_scope(compiler);
}

static void visit_TableComp(struct Compiler *const compiler, const struct Node *const node) {
    enter_scope(compiler);

    visit(compiler, node->children[1]->children[1]);

    bb_add_byte(compiler->buffer, INITFOR);
    bb_add_byte(compiler->buffer, END);

    if (node->children[1]->nodetype == N_LETITER) {
        decl_var(compiler, node->children[1]->children[0]->name, node->children[1]->children[0]->name_len);
    } else if (!contains_var(compiler, node->children[1]->children[0]->name, node->children[1]->children[0]->name_len)){
        YASL_PRINT_ERROR_UNDECLARED_VAR(node->children[1]->children[0]->name, node->children[1]->children[0]->line);
        handle_error(compiler);
        exit_scope(compiler);
        return;
    }

    int64_t index_start = compiler->buffer->count;

    bb_add_byte(compiler->buffer, ITER_1);

    int64_t index_second;
    enter_conditional_false(compiler, &index_second);

    store_var(compiler, node->children[1]->children[0]->name, node->children[1]->children[0]->name_len, node->line);

    if (node->children[2]) {
        int64_t index_third;
        visit(compiler, node->children[2]);
        enter_conditional_false(compiler, &index_third);

        visit(compiler, TableComp_get_key_value(node)->children[0]);
        visit(compiler, TableComp_get_key_value(node)->children[1]);

        exit_conditional_false(compiler, &index_third);
    } else {
        visit(compiler, TableComp_get_key_value(node)->children[0]);
        visit(compiler, TableComp_get_key_value(node)->children[1]);
    }

    branch_back(compiler, index_start);

    exit_conditional_false(compiler, &index_second);

    bb_add_byte(compiler->buffer, NEWTABLE);
    bb_add_byte(compiler->buffer, ENDCOMP);

    exit_scope(compiler);
}

static void visit_ForIter(struct Compiler *const compiler, const struct Node *const node) {
    /* Currently only implements case, at global scope:
     *
     * for let x in y { ... }
     *
     */
    enter_scope(compiler);

    visit(compiler, node->children[0]->children[1]);

    bb_add_byte(compiler->buffer, INITFOR);

    if (node->children[0]->nodetype == N_LETITER) {
        decl_var(compiler, node->children[0]->children[0]->name, node->children[0]->children[0]->name_len);
    } else if (!contains_var(compiler, node->children[0]->children[0]->name, node->children[0]->children[0]->name_len)){
        YASL_PRINT_ERROR_UNDECLARED_VAR(node->children[0]->children[0]->name, node->children[0]->children[0]->line);
        handle_error(compiler);
        exit_scope(compiler);
        return;
    }

    int64_t index_start = compiler->buffer->count;
    add_checkpoint(compiler, index_start);

    bb_add_byte(compiler->buffer, ITER_1);

    add_checkpoint(compiler, compiler->buffer->count);

    int64_t index_second;
    enter_conditional_false(compiler, &index_second);


    store_var(compiler, node->children[0]->children[0]->name, node->children[0]->children[0]->name_len, node->line);

    visit(compiler, ForIter_get_body(node));

    branch_back(compiler, index_start);

    exit_conditional_false(compiler, &index_second);

    bb_add_byte(compiler->buffer, ENDFOR);
    exit_scope(compiler);

    rm_checkpoint(compiler);
    rm_checkpoint(compiler);
}

static void visit_While(struct Compiler *const compiler, const struct Node *const node) {
    int64_t index_start = compiler->buffer->count;

    if (node->children[2] != NULL) {
        bb_add_byte(compiler->buffer, BR_8);
        int64_t index = compiler->buffer->count;
        bb_intbytes8(compiler->buffer, 0);
        index_start = compiler->buffer->count;
        //if (node->children[2] != NULL) {
            visit(compiler, node->children[2]);
        //}
        bb_rewrite_intbytes8(compiler->buffer, index, compiler->buffer->count - index - 8);
    }

    add_checkpoint(compiler, index_start);

    visit(compiler, While_get_cond(node));

    add_checkpoint(compiler, compiler->buffer->count);

    int64_t index_second;
    enter_conditional_false(compiler, &index_second);
    enter_scope(compiler);

    visit(compiler, While_get_body(node));

    branch_back(compiler, index_start);

    exit_scope(compiler);
    exit_conditional_false(compiler, &index_second);

    rm_checkpoint(compiler);
    rm_checkpoint(compiler);
}

static void visit_Break(struct Compiler *const compiler, const struct Node *const node) {
    if (compiler->checkpoints_count == 0) {
        YASL_PRINT_ERROR_SYNTAX("break outside of loop (line %zd).\n", node->line);
        handle_error(compiler);
        return;
    }
    bb_add_byte(compiler->buffer, BCONST_F);
    branch_back(compiler, break_checkpoint(compiler));
    // bb_add_byte(compiler->buffer, BR_8);
    // bb_intbytes8(compiler->buffer, break_checkpoint(compiler) - compiler->buffer->count - 8);
}

static void visit_Continue(struct Compiler *const compiler, const struct Node *const node) {
    if (compiler->checkpoints_count == 0) {
        YASL_PRINT_ERROR_SYNTAX("continue outside of loop (line %zd).\n", node->line);
        handle_error(compiler);
        return;
    }
    branch_back(compiler, continue_checkpoint(compiler));
}

static void visit_If(struct Compiler *const compiler, const struct Node *const node) {
    visit(compiler, node->children[0]);

    int64_t index_then;
    enter_conditional_false(compiler, &index_then);
    enter_scope(compiler);

    visit(compiler, node->children[1]);

    int64_t index_else = 0;

    if (node->children[2] != NULL) {
        bb_add_byte(compiler->buffer, BR_8);
        index_else = compiler->buffer->count;
        bb_intbytes8(compiler->buffer, 0);
    }

    exit_scope(compiler);
    exit_conditional_false(compiler, &index_then);

    if (node->children[2] != NULL) {
        enter_scope(compiler);
        visit(compiler, node->children[2]);
        exit_scope(compiler);
        bb_rewrite_intbytes8(compiler->buffer, index_else, compiler->buffer->count-index_else-8);
    }
}

static void visit_Print(struct Compiler *const compiler, const struct Node *const node) {
    visit(compiler, Print_get_expr(node));
    bb_add_byte(compiler->buffer, PRINT);
}

static void declare_with_let_or_const(struct Compiler *const compiler, const struct Node *const node) {
    if (contains_var_in_current_scope(compiler, node->name, node->name_len)) {
        YASL_PRINT_ERROR_SYNTAX("Illegal redeclaration of %s in line %zd.\n", node->name, node->line);
        handle_error(compiler);
        return;
    }

    decl_var(compiler, node->name, node->name_len);

    if (Let_get_expr(node) != NULL) visit(compiler, Let_get_expr(node));
    else bb_add_byte(compiler->buffer, NCONST);

    store_var(compiler, node->name, node->name_len, node->line);
}

static void visit_Let(struct Compiler *const compiler, const struct Node *const node) {
    declare_with_let_or_const(compiler, node);
}

static void visit_Const(struct Compiler *const compiler, const struct Node *const node) {
    declare_with_let_or_const(compiler, node);
    make_const(compiler, node->name, node->name_len);
}

static void visit_TriOp(struct Compiler *const compiler, const struct Node *const node) {
    visit(compiler, node->children[0]);

    int64_t index_l;
    enter_conditional_false(compiler, &index_l);

    visit(compiler, node->children[1]);

    bb_add_byte(compiler->buffer, BR_8);
    int64_t index_r = compiler->buffer->count;
    bb_intbytes8(compiler->buffer, 0);

    exit_conditional_false(compiler, &index_l);

    visit(compiler, node->children[2]);
    bb_rewrite_intbytes8(compiler->buffer, index_r, compiler->buffer->count-index_r-8);
}

static void visit_BinOp(struct Compiler *const compiler, const struct Node *const node) {
    // complicated bin ops are handled on their own.
    if (node->type == T_DQMARK) {     // ?? operator
        visit(compiler, node->children[0]);
        bb_add_byte(compiler->buffer, DUP);
        bb_add_byte(compiler->buffer, BRN_8);
        int64_t index = compiler->buffer->count;
        bb_intbytes8(compiler->buffer, 0);
        bb_add_byte(compiler->buffer, POP);
        visit(compiler, node->children[1]);
        bb_rewrite_intbytes8(compiler->buffer, index, compiler->buffer->count-index-8);
        return;
    } else if (node->type == T_DBAR) {  // or operator
        visit(compiler, node->children[0]);
        bb_add_byte(compiler->buffer, DUP);
        bb_add_byte(compiler->buffer, BRT_8);
        int64_t index = compiler->buffer->count;
        bb_intbytes8(compiler->buffer, 0);
        bb_add_byte(compiler->buffer, POP);
        visit(compiler, node->children[1]);
        bb_rewrite_intbytes8(compiler->buffer, index, compiler->buffer->count-index-8);
        return;
    } else if (node->type == T_DAMP) {   // and operator
        visit(compiler, node->children[0]);
        bb_add_byte(compiler->buffer, DUP);

        int64_t index;
        enter_conditional_false(compiler, &index);

        bb_add_byte(compiler->buffer, POP);
        visit(compiler, node->children[1]);
        exit_conditional_false(compiler, &index);
        return;
    }
    // all other operators follow the same pattern of visiting one child then the other.
    visit(compiler, node->children[0]);
    visit(compiler, node->children[1]);
    switch(node->type) {
        case T_BAR:
            bb_add_byte(compiler->buffer, BOR);
            break;
        case T_CARET:
            bb_add_byte(compiler->buffer, BXOR);
            break;
        case T_AMP:
            bb_add_byte(compiler->buffer, BAND);
            break;
        case T_AMPCARET:
            bb_add_byte(compiler->buffer, BANDNOT);
            break;
        case T_DEQ:
            bb_add_byte(compiler->buffer, EQ);
            break;
        case T_TEQ:
            bb_add_byte(compiler->buffer, ID);
            break;
        case T_BANGEQ:
            bb_add_byte(compiler->buffer, EQ);
            bb_add_byte(compiler->buffer, NOT);
            break;
        case T_BANGDEQ:
            bb_add_byte(compiler->buffer, ID);
            bb_add_byte(compiler->buffer, NOT);
            break;
        case T_GT:
            bb_add_byte(compiler->buffer, GT);
            break;
        case T_GTEQ:
            bb_add_byte(compiler->buffer, GE);
            break;
        case T_LT:
            bb_add_byte(compiler->buffer, GE);
            bb_add_byte(compiler->buffer, NOT);
            break;
        case T_LTEQ:
            bb_add_byte(compiler->buffer, GT);
            bb_add_byte(compiler->buffer, NOT);
            break;
        case T_TILDE:
            bb_add_byte(compiler->buffer, CNCT);
            break;
        case T_DGT:
            bb_add_byte(compiler->buffer, BSR);
            break;
        case T_DLT:
            bb_add_byte(compiler->buffer, BSL);
            break;
        case T_PLUS:
            bb_add_byte(compiler->buffer, ADD);
            break;
        case T_MINUS:
            bb_add_byte(compiler->buffer, SUB);
            break;
        case T_STAR:
            bb_add_byte(compiler->buffer, MUL);
            break;
        case T_SLASH:
            bb_add_byte(compiler->buffer, FDIV);
            break;
        case T_DSLASH:
            bb_add_byte(compiler->buffer, IDIV);
            break;
        case T_MOD:
            bb_add_byte(compiler->buffer, MOD);
            break;
        case T_DSTAR:
            bb_add_byte(compiler->buffer, EXP);
            break;
        default:
            puts("error in visit_BinOp");
            exit(EXIT_FAILURE);
    }
}

static void visit_UnOp(struct Compiler *const compiler, const struct Node *const node) {
    visit(compiler, node->children[0]);
    switch(node->type) {
        case T_PLUS:
            bb_add_byte(compiler->buffer, NOP);
            break;
        case T_MINUS:
            bb_add_byte(compiler->buffer, NEG);
            break;
        case T_BANG:
            bb_add_byte(compiler->buffer, NOT);
            break;
        case T_CARET:
            bb_add_byte(compiler->buffer, BNOT);
            break;
        case T_LEN:
            bb_add_byte(compiler->buffer, LEN);
            break;
        default:
            puts("error in visit_UnOp");
            exit(EXIT_FAILURE);
    }
}

static void visit_Assign(struct Compiler *const compiler, const struct Node *const node) {
    if (!contains_var(compiler, node->name, node->name_len)) {
        YASL_PRINT_ERROR_UNDECLARED_VAR(node->name, node->line);
        handle_error(compiler);
        return;
    }
    visit(compiler, node->children[0]);
    bb_add_byte(compiler->buffer, DUP);
    store_var(compiler, node->name, node->name_len, node->line);
}

static void visit_Var(struct Compiler *const compiler, const struct Node *const node) {
    load_var(compiler, node->name, node->name_len, node->line);
}

static void visit_Undef(struct Compiler *const compiler, const struct Node *const node) {
    bb_add_byte(compiler->buffer, NCONST);
}

static void visit_Float(struct Compiler *const compiler, const struct Node *const node) {
    YASL_TRACE_LOG("float64: %s\n", node->name);
    if (strlen("nan") == node->name_len && !memcmp(node->name, "nan", node->name_len)) bb_add_byte(compiler->buffer, DCONST_N);
    else if (strlen("inf") == node->name_len && !memcmp(node->name, "inf", node->name_len)) bb_add_byte(compiler->buffer, DCONST_I);
    else {
        bb_add_byte(compiler->buffer, DCONST);
        bb_floatbytes8(compiler->buffer, strtod(node->name, (char**)NULL));
    }
}

static void visit_Integer(struct Compiler *const compiler, const struct Node *const node) {
    bb_add_byte(compiler->buffer, ICONST);
    YASL_TRACE_LOG("int64: %s\n", node->name);
    if (node->name_len < 2) {
        bb_intbytes8(compiler->buffer, (int64_t)strtoll(node->name, (char**)NULL, 10));
        return;
    }
    switch(node->name[1]) {
        case 'x':
            bb_intbytes8(compiler->buffer, (int64_t)strtoll(node->name+2, (char**)NULL, 16));
            break;
        case 'b':
            bb_intbytes8(compiler->buffer, (int64_t)strtoll(node->name+2, (char**)NULL, 2));
            break;
        default:
            bb_intbytes8(compiler->buffer, (int64_t)strtoll(node->name, (char**)NULL, 10));
            break;
    }
}

static void visit_Boolean(struct Compiler *const compiler, const struct Node *const node) {
    if (!memcmp(node->name, "true", node->name_len)) {
        bb_add_byte(compiler->buffer, BCONST_T);
        return;
    } else if (!memcmp(node->name, "false", node->name_len)) {
        bb_add_byte(compiler->buffer, BCONST_F);
        return;
    }
}

static void visit_String(struct Compiler *const compiler, const struct Node *const node) {
    struct YASL_Object *value = table_search_string_int(compiler->strings, node->name, node->name_len);
    if (value == NULL) {
        YASL_DEBUG_LOG("%s\n", "caching string");
        table_insert_string_int(compiler->strings, node->name, node->name_len, compiler->header->count);
        bb_intbytes8(compiler->header, node->name_len);
        bb_append(compiler->header, (unsigned char*)node->name, node->name_len);
    }

    value = table_search_string_int(compiler->strings, node->name, node->name_len);

    bb_add_byte(compiler->buffer, NEWSTR);
    bb_intbytes8(compiler->buffer, value->value.ival);
}

static void make_new_collection(struct Compiler *const compiler, const struct Node *const node, Opcode type) {
    bb_add_byte(compiler->buffer, END);
    visit_Body(compiler, node);
    bb_add_byte(compiler->buffer, type);
}

static void visit_List(struct Compiler *const compiler, const struct Node *const node) {
    make_new_collection(compiler, List_get_values(node), NEWLIST);
}

static void visit_Table(struct Compiler *const compiler, const struct Node *const node) {
    make_new_collection(compiler, Table_get_values(node), NEWTABLE);
}

// NOTE: must keep this synced with the enum in ast.h
static void (*jumptable[])(struct Compiler *const, const struct Node *const) = {
        &visit_ExprStmt,
        &visit_Block,
        &visit_Body,
        &visit_FunctionDecl,
        &visit_Return,
        &visit_Call,
        &visit_Set,
        &visit_Get,
        NULL,
        &visit_ListComp,
        &visit_TableComp,
        &visit_ForIter,
        &visit_While,
        &visit_Break,
        &visit_Continue,
        &visit_If,
        &visit_Print,
        &visit_Let,
        &visit_Const,
        &visit_TriOp,
        &visit_BinOp,
        &visit_UnOp,
        &visit_Assign,
        &visit_Var,
        &visit_Undef,
        &visit_Float,
        &visit_Integer,
        &visit_Boolean,
        &visit_String,
        &visit_List,
        &visit_Table
};

static void visit(struct Compiler *const compiler, const struct Node *const node) {
    jumptable[node->nodetype](compiler, node);
}