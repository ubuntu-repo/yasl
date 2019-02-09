#include "unoptest.h"
#include "yats.h"

SETUP_YATS();

static void test_neg() {
	unsigned char expected[] = {
		0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		ICONST,
		0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		GSTORE_1, 0x00,
		GLOAD_1, 0x00,
		NEG,
		POP,
		HALT
	};
	ASSERT_GEN_BC_EQ(expected, "x := 16; -x;");
}

static void test_len() {
	unsigned char expected[] = {
		0x1C, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		'Y', 'A', 'S', 'L',
		NEWSTR,
		0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		GSTORE_1, 0x00,
		GLOAD_1, 0x00,
		LEN,
		POP,
		HALT
	};
	ASSERT_GEN_BC_EQ(expected, "x := 'YASL'; len x;");
}

static void test_not() {
	unsigned char expected[] = {
		0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		BCONST_T,
		GSTORE_1, 0x00,
		GLOAD_1, 0x00,
		NOT,
		POP,
		HALT
	};
	ASSERT_GEN_BC_EQ(expected, "x := true; !x;");
}

static void test_bnot() {
	unsigned char expected[] = {
		0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		ICONST_0,
		GSTORE_1, 0x00,
		GLOAD_1, 0x00,
		BNOT,
		POP,
		HALT
	};
	ASSERT_GEN_BC_EQ(expected, "x := 0x00; ^x;");
}

int unoptest(void) {
	test_len();
	test_neg();
	test_not();
	test_bnot();

	return __YASL_TESTS_FAILED__;
}
