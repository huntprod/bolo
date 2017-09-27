#ifndef BOLO_ERRNO_H
#define BOLO_ERRNO_H

#define BOLO_ERROR_BASE 512
#define __bolo_errno(n) ((n) + BOLO_ERROR_BASE)

#define BOLO_EUNKNOWN  __bolo_errno(0)
#define BOLO_ENOTSET   __bolo_errno(1)
#define BOLO_EBADHASH  __bolo_errno(2)
#define BOLO_EBADTREE  __bolo_errno(3)
#define BOLO_EBADSLAB  __bolo_errno(4)
#define BOLO_EBLKFULL  __bolo_errno(5)
#define BOLO_EBLKRANGE __bolo_errno(6)
#define BOLO_ENOMAINDB __bolo_errno(7)

#define BOLO_ERROR_TOP __bolo_errno(7)

#endif
