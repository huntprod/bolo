#include "bolo.h"

void
bail(const char *msg)
{
	fprintf(stderr, "%s\n", msg);
	exit(2);
}

static const char *errors[] = {
	/* BOLO_EUNKNOWN */  "An unknown error has occurred",
	/* BOLO_ENOTSET */   "Key not set in hash table",
	/* BOLO_EBADHASH */  "Corrupt hash table detected",
	/* BOLO_EBADTREE */  "Corrupt btree detected",
	/* BOLO_EBADSLAB */  "Corrupt database slab detected",
	/* BOLO_EBLKFULL */  "Database block is full",
	/* BOLO_EBLKRANGE */ "Database block range insufficient",
	/* BOLO_ENOMAINDB */ "main.db index not found in database root",
};

const char * error(int num)
{
	if (num <= BOLO_ERROR_BASE)
		return strerror(num);

	if (num > BOLO_ERROR_TOP)
		num = BOLO_EUNKNOWN;

	return errors[num];
}
