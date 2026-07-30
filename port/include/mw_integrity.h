#ifndef MW_INTEGRITY_H
#define MW_INTEGRITY_H

#include <stddef.h>

/* Verify approved size/CRC-32/SHA-256 executable variants beside the port. */
int integrity_verify_original_executables(const char *directory,
                                          char *error, size_t error_size);

#endif /* MW_INTEGRITY_H */
