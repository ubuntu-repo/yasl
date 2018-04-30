#include "compiler.h"



Compiler *compiler_new(Parser* parser) {
    Compiler *compiler = malloc(sizeof(Compiler));
    /*compiler->globals = env_new();
    compiler->locals = env_new();
    compiler->header = NULL;
    compiler->header_len = 0;
    compiler->code = NULL;
    compiler->code_len = 0; */
    compiler->parser = parser;
    compiler->buffer = bb_new(16);
    compiler->header = bb_new(16);
    compiler->header->count = 16;
    //printf("compiler->header->count is %d\n", compiler->header->count);
    compiler->code   = bb_new(16);
    return compiler;
};

void compiler_del(Compiler *compiler) {
    /*env_del(compiler->globals);
    env_del(compiler->locals);
    free(compiler->header);
    free(compiler->code); */
    //puts("deleting buffer");
    bb_del(compiler->buffer);
    //puts("deleting header");
    bb_del(compiler->header);
    //puts("deleting code");
    bb_del(compiler->code);
    parser_del(compiler->parser);
    free(compiler);
};

void compile(Compiler *compiler) {
    Node *node;
    while (!peof(compiler->parser)) {
            gettok(compiler->parser->lex);
            if (peof(compiler->parser)) break;
            node = parse(compiler->parser);
            //puts("parsed");
            //printf("compiler->header->count is %d\n", compiler->header->count);
            visit(compiler, node);
            //puts("visited");
            //printf("compiler->header->count is %d\n", compiler->header->count);
            bb_append(compiler->code, compiler->buffer->bytes, compiler->buffer->count);
            //puts("appended");
            //printf("compiler->header->count is %d\n", compiler->header->count);
            compiler->buffer->count = 0;
            //printf("compiler->header->count is %d\n", compiler->header->count);
            node_del(node);
            //printf("compiler->header->count is %d\n", compiler->header->count);
            //puts("deleted node");
    }
    //puts("ready to calculate header");
    //printf("compiler->header->count is %d\n", compiler->header->count);
    //memcpy(compiler->header->bytes, &compiler->header->count, sizeof(int64_t));
    //puts("calculated header");
    int i = 0;
    for (i = 0; i < compiler->header->count; i++) {
        printf("%02x\n", compiler->header->bytes[i]);// && 0xFF);
    }
    puts("entry point");
    for (i = 0; i < compiler->code->count; i++) {
        printf("%02x\n", compiler->code->bytes[i]);// & 0xFF);
    }
    printf("%02x\n", HALT);
    FILE *fp = fopen("../source.yb", "wb");
    if (!fp) exit(EXIT_FAILURE);
    fwrite(compiler->header->bytes, 1, compiler->header->count, fp);
    fwrite(compiler->code->bytes, 1, compiler->code->count, fp);
    fputc(HALT, fp);
    fclose(fp);
}

void visit_Print(Compiler* compiler, Node *node) {
    //printf("compiler->header->count is %d\n", compiler->header->count);
    visit(compiler, node->children[0]);
    char print_bytes[] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};      // TODO: lookup in table
    bb_add_byte(compiler->buffer, BCALL_8);
    bb_append(compiler->buffer, print_bytes, 8);
}

void visit_BinOp(Compiler *compiler, Node *node) {
    //printf("compiler->header->count is %d\n", compiler->header->count);
    switch(node->type) {
        case TOK_BAR:
            visit(compiler, node->children[0]);
            visit(compiler, node->children[1]);
            bb_add_byte(compiler->buffer, BOR);
            break;
        case TOK_TILDE:
            visit(compiler, node->children[0]);
            visit(compiler, node->children[1]);
            bb_add_byte(compiler->buffer, BXOR);
            break;
        case TOK_AMP:
            visit(compiler, node->children[0]);
            visit(compiler, node->children[1]);
            bb_add_byte(compiler->buffer, BAND);
            break;
        case TOK_DEQ:
            visit(compiler, node->children[0]);
            visit(compiler, node->children[1]);
            bb_add_byte(compiler->buffer, EQ);
            break;
        case TOK_TEQ:
            visit(compiler, node->children[0]);
            visit(compiler, node->children[1]);
            bb_add_byte(compiler->buffer, ID);
            break;
        case TOK_BANGEQ:
            visit(compiler, node->children[0]);
            visit(compiler, node->children[1]);
            bb_add_byte(compiler->buffer, EQ);
            bb_add_byte(compiler->buffer, NOT);
            break;
        case TOK_BANGDEQ:
            visit(compiler, node->children[0]);
            visit(compiler, node->children[1]);
            bb_add_byte(compiler->buffer, ID);
            bb_add_byte(compiler->buffer, NOT);
            break;
        default:
            puts("error in visit_BinOp");
            exit(EXIT_FAILURE);
    }
}

void visit_String(Compiler* compiler, Node *node) {
    //printf("compiler->header->count is %d\n", compiler->header->count);
    // TODO: store string we've already seen.
    bb_add_byte(compiler->buffer, NEWSTR8);
    bb_intbytes8(compiler->buffer, node->name_len);
    bb_intbytes8(compiler->buffer, compiler->header->count);
    bb_append(compiler->header, node->name, node->name_len);
    //printf("compiler->header->count is %d\n", compiler->header->count);
}

void visit_Integer(Compiler *compiler, Node *node) {
    //printf("compiler->header->count is %d\n", compiler->header->count);
    bb_add_byte(compiler->buffer, ICONST);
    switch(node->name[1]) {
        case 'x':
            bb_intbytes8(compiler->buffer, (int64_t)strtoll(node->name+2, (char**)NULL, 16));
            break;
        case 'b':
            bb_intbytes8(compiler->buffer, (int64_t)strtoll(node->name+2, (char**)NULL, 2));
            break;
        case 'o':
            bb_intbytes8(compiler->buffer, (int64_t)strtoll(node->name+2, (char**)NULL, 8));
            break;
        default:
            bb_intbytes8(compiler->buffer, (int64_t)strtoll(node->name, (char**)NULL, 10));
            break;
    }
}

void visit(Compiler* compiler, Node* node) {
    //printf("compiler->header->count is %d\n", compiler->header->count);
    switch(node->nodetype) {
    case NODE_PRINT:
        visit_Print(compiler, node);
        break;
    case NODE_BINOP:
        visit_BinOp(compiler, node);
        break;
    case NODE_INT64:
        visit_Integer(compiler, node);
        break;
    case NODE_STR:
        visit_String(compiler, node);
        break;
    default:
        puts("unknown node type");
        exit(1);
    }
}