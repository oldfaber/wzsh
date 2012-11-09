/*
 * No copyright. This trivial file is offered as-is, without any warranty.
 */

/* clipboard.h -- clipboard interface */

#ifndef CLIPBOARD_H
#define CLIPBOARD_H

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

extern int is_dev_clipboard_active;

#ifdef	__cplusplus
extern "C" {
#endif

int clipboard_paste(void);
int clipboard_copy(const wchar_t *st);
void init_clipboard(void);
HANDLE create_clip_writer_thread(void);
HANDLE create_clip_reader_thread(void);

#ifdef	__cplusplus
}
#endif

#endif
