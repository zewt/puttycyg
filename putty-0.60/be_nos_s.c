/*
 * Linking module for PuTTYtel: list the available backends not
 * including ssh.
 */

#include <stdio.h>
#include "putty.h"

const int be_default_protocol = PROT_CYGTERM;

const char *const appname = "PuTTYCyg";

struct backend_list backends[] = {
    {PROT_CYGTERM, "cygterm", &cygterm_backend},
    {0, NULL}
};
