#ifndef SYSTEM_LOADER_H
#define SYSTEM_LOADER_H

#include <efi.h>
#include <efilib.h>

#include "محمل_النواه.h"

EFI_STATUS load_system_archive(
    EFI_FILE_HANDLE Root,
    CHAR16         *Path,
    boot_info_t *boot);

#endif
