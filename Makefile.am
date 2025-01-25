
ACLOCAL_AMFLAGS = -I m4

bin_PROGRAMS = sequence
sequence_SOURCES = sequence.c

EXTRA_DIST = sequence.spec
dist_man_MANS = sequence.1

sequence.1: sequence
	which txt2man && ./sequence --help | txt2man -s 1 -t "${PACKAGE_NAME}" -r "${PACKAGE_NAME}-${PACKAGE_VERSION}" > sequence.1 || true

