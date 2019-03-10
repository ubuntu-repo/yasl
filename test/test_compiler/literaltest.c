#include "literaltest.h"
#include "yats.h"

SETUP_YATS();

static void test_elimination() {
	unsigned char expected[] = {
		0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		HALT
	};
	ASSERT_GEN_BC_EQ(expected, "undef; true; 'YASL'; 10; 10.0;");
}

static void test_undef() {
    unsigned char expected[] = {
            0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            NCONST,
            PRINT,
            HALT
    };
    ASSERT_GEN_BC_EQ(expected, "echo undef;");
}

static void test_true() {
    unsigned char expected[] = {
            0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            BCONST_T,
            PRINT,
            HALT
    };
    ASSERT_GEN_BC_EQ(expected, "echo true;");
}

static void test_false() {
    unsigned char expected[] = {
            0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            BCONST_F,
            PRINT,
            HALT
    };
    ASSERT_GEN_BC_EQ(expected, "echo false;");
}

static void test_small_ints() {
    unsigned char expected[]  = {
            0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            ICONST_M1,
            PRINT,
            ICONST_0,
            PRINT,
            ICONST_1,
            PRINT,
            ICONST_2,
            PRINT,
            ICONST_3,
            PRINT,
            ICONST_4,
            PRINT,
            ICONST_5,
            PRINT,
            HALT
    };
    ASSERT_GEN_BC_EQ(expected, "echo -1; echo 0; echo 1; echo 2; echo 3; echo 4; echo 5;");
}

static void test_bin() {
    unsigned char expected[] = {
            0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            ICONST_3,
            PRINT,
            HALT
    };
    ASSERT_GEN_BC_EQ(expected, "echo 0b11;");
}

static void test_dec() {
    unsigned char expected[]  = {
            0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            ICONST,
            0x0A, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            PRINT,
            HALT
    };
    ASSERT_GEN_BC_EQ(expected, "echo 10;");
}

static void test_hex() {
    unsigned char expected[] = {
            0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            ICONST,
            0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            PRINT,
            HALT
    };
    ASSERT_GEN_BC_EQ(expected, "echo 0x10;");
}

static void test_small_floats() {
	unsigned char expected[]  = {
		0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		DCONST_0,
		PRINT,
		DCONST_1,
		PRINT,
		DCONST_2,
		PRINT,
		HALT
	};
	ASSERT_GEN_BC_EQ(expected, "echo 0.0; echo 1.0; echo 2.0;");
}
static void test_float() {
    unsigned char expected[] = {
            0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            DCONST,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xF8, 0x3F,
            PRINT,
            HALT
    };
    ASSERT_GEN_BC_EQ(expected, "echo 1.5;");
}

static void test_string() {
    unsigned char expected[] = {
            0x1C, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            'Y', 'A', 'S', 'L',
            NEWSTR,
            0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            PRINT,
            HALT
    };
    ASSERT_GEN_BC_EQ(expected, "echo 'YASL';");
}

static void test_list() {
    unsigned char expected[] = {
            0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            END,
            ICONST_0,
            ICONST_1,
            ICONST_2,
            ICONST_3,
            ICONST_4,
            NEWLIST,
            POP,
            HALT
    };
    ASSERT_GEN_BC_EQ(expected, "[0, 1, 2, 3, 4];");
}

static void test_table() {
    unsigned char expected[] = {
            0x27, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            'z',  'e',  'r',  'o',
            0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            'o',  'n',  'e',
            END,
            ICONST_0,
            NEWSTR,
            0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            ICONST_1,
            NEWSTR,
            0x1C, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            NEWTABLE,
            POP,
            HALT
    };
    ASSERT_GEN_BC_EQ(expected, "{0:'zero', 1:'one'};");
}

int literaltest(void) {
	test_undef();
	test_true();
	test_false();
	test_small_ints();
	test_bin();
	test_dec();
	test_hex();
	test_small_floats();
	test_float();
	test_string();
	test_list();
	test_table();
	test_elimination();

	return __YASL_TESTS_FAILED__;
}
