#ifndef MW_INTEGRITY_H
#define MW_INTEGRITY_H

#include <stddef.h>

/* Verify the exact original executables required beside the native port. */
int integrity_verify_original_executables(const char *directory,
                                          char *error, size_t error_size);

#endif /* MW_INTEGRITY_H */
