/*
 * No copyright. This trivial file is offered as-is, without any warranty.
 */

/* shell_init.h -- SHELL initialization functions */


#if !defined(SHELL_INIT_H)
#define SHELL_INIT_H

char *path_to_slash(char *nt_path);
void shell_init(void);
void set_HOME_and_PATH(void);

/* from tconsole.c */
void nt_init_term(void);

#endif
