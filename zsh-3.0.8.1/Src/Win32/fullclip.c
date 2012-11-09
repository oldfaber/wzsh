/*-
 * Copyright (c) 1980, 1991 The Regents of the University of California.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*
 * clip.c : support for clipboard functions.
 * -amol
 *
 * from
 * $Header: /p/tcsh/cvsroot/tcsh/win32/clip.c,v 1.9 2006/03/05 08:59:36 amold Exp $
 *
 * f/f: quick and dirty fixes for compilation/link
 *      removed e_page_xxx() these functions don't belong to clipboard handling
 *      removed e_dosify_xxx() these functions don't belong to clipboard handling
 *      clipper_thread() renamed clip_thread() is static
 *      renamed e_copy_to_clipboard() to clipboard_copy() and modified
 */

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <stdio.h>
#include <stdlib.h>
#include <wchar.h>

#include "forklib.h"
#include "ntdbg.h"
#include "clipboard.h"

#define STKSIZE		131072  /* clipboard thread stack */

#define	CHAR		0x00FFFFFF

static void clip_thread(void);
/* could the following static functions ? */
BOOL InitInstance(HINSTANCE, int);
LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);


static HWND ghwndmain;

extern int ctrl_handler(DWORD);



BOOL InitApplication(HINSTANCE hInstance)
{
	WNDCLASS  wc;


	// Fill in window class structure with parameters that describe
	// the main window.
	wc.style         = CS_HREDRAW | CS_VREDRAW;
	wc.lpfnWndProc   = (WNDPROC)WndProc;
	wc.cbClsExtra    = 0;
	wc.cbWndExtra    = 0;
	wc.hInstance     = hInstance;
	wc.hIcon         = NULL;//LoadIcon (hInstance, szAppName);
	wc.hCursor       = NULL;//LoadCursor(NULL, IDC_ARROW);
	wc.hbrBackground = (HBRUSH)IntToPtr(COLOR_WINDOW+1);

	wc.lpszMenuName  = NULL;
	wc.lpszClassName = "zshclipboard";

	return RegisterClass(&wc);
}


void init_clipboard(void)
{
	HANDLE ht;
	DWORD tid;

	ht = CreateThread(NULL, STKSIZE, (LPTHREAD_START_ROUTINE)clip_thread, NULL, 0, &tid);
	if (!ht)
		/* @@@@ why abort(): simply ignore the clipboard ??? */
                /* abort() */
		dbgprintf(PR_ERROR, "!!! %s(): error %ld\n", __FUNCTION__, GetLastError());
	CloseHandle(ht);
}


/* 
 * Creating a hidden window may not be strictly necessary on
 * NT, but why tempt fate ?
 * -amol
 */

static void clip_thread(void)
{
	MSG msg;
	HINSTANCE hInstance = GetModuleHandle(NULL);

	if (!InitApplication(hInstance)) {
		return;
	}
	if (!InitInstance(hInstance, 0)) {
		return;
	}
	// Main message loop:
	while (GetMessage(&msg, NULL, 0, 0)) {
		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}
	if ( !ctrl_handler(CTRL_CLOSE_EVENT))
		init_clipboard();
	return;
}


BOOL InitInstance(HINSTANCE hInstance, int nCmdShow)
{
	HWND hWnd;

	UNREFERENCED_PARAMETER(nCmdShow);

	hWnd = CreateWindow("tcshclipboard", "tcshclipboard",
			    WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, 0, CW_USEDEFAULT, 0,
			    NULL, NULL, hInstance, NULL);
	if (!hWnd) {
		return (FALSE);
	}
	UpdateWindow(hWnd);
	ghwndmain = hWnd;
	return (TRUE);
}


LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
	switch (message) {
	case WM_DESTROYCLIPBOARD:
		break;
	case WM_DESTROY:
		PostQuitMessage(0);
		break;
	default:
		return (DefWindowProc(hWnd, message, wParam, lParam));
	}
	return (0);
}


/* copy the passed string to the clipboard */
int clipboard_copy(const wchar_t *st)
{
	unsigned char *cbp;
	const wchar_t *kp;
	int err;
	size_t len;
	unsigned char *clipbuf;
	HANDLE hclipbuf;

	if (!ghwndmain)	
		return (-1);
	
	len = wcslen(st);

	hclipbuf = GlobalAlloc(GMEM_MOVEABLE|GMEM_DDESHARE, len + 2);
	if (!hclipbuf)
		return (-1);
	clipbuf = (unsigned char *)GlobalLock(hclipbuf);

	if (!clipbuf) {
		err = GetLastError();
		GlobalFree(hclipbuf);
		return (-1);
	}
	
	kp = st;
	cbp = clipbuf;
	while (*kp != 0) {
		*cbp = (unsigned char)(*kp & CHAR);
		cbp++;
		kp++;
	}
	*cbp = 0;

	GlobalUnlock(clipbuf);

	if (!OpenClipboard(ghwndmain))
		goto error;

	if (!EmptyClipboard())
		goto error;
		
	if (SetClipboardData(CF_TEXT, hclipbuf) != hclipbuf){
		err = GetLastError();
		goto error;

	}

	CloseClipboard();
	return (0);

error:
	GlobalFree(hclipbuf);
	CloseClipboard();
	return (-1);
}


/* use zle_utils.c::setline() to set the shell line */
int clipboard_paste(void)
{
	HANDLE hclip;
	unsigned char *clipbuf;


	if (!ghwndmain)	
		return (-1);
	if (!IsClipboardFormatAvailable(CF_TEXT))
		return (-1);
	
	if (!OpenClipboard(ghwndmain))
		return (-1);
	
	hclip = GetClipboardData(CF_TEXT);
	if (hclip) {
		clipbuf = (unsigned char *)GlobalLock(hclip);
                /* copy the clipboard data and call setline() ? */
		/* clipboard data is wide char ? */
		GlobalUnlock(hclip);
	}
	CloseClipboard();

	return (0);
}

int is_dev_clipboard_active=0;
HANDLE ghdevclipthread;

/* Reads from pipe and write to clipboard */
void clip_writer_proc(HANDLE hinpipe)
{
	unsigned char *realbuf;
	unsigned char *clipbuf;
	unsigned char *ptr;
	DWORD bread=0,spleft,err,i,rbsize;
	DWORD ptrloc;
	HANDLE hclipbuf;


	rbsize = 4096;
	realbuf = heap_alloc(rbsize);
	ptr = realbuf;
	ptrloc = 0;
	spleft = rbsize;

	while (spleft) {
		if (!ReadFile(hinpipe, ptr, spleft, &bread, NULL)) {
			spleft = GetLastError();
			dbgprintf(PR_WARN, "!!! %s(): ReadFile(0x%p, ..) error %ld\n", __FUNCTION__, hinpipe, spleft);
			if (spleft == ERROR_BROKEN_PIPE)
				break;
		}
		if (bread == 0)
			break;
		ptr += bread;
		ptrloc += bread;
		spleft -=bread;

		if (spleft <=0){
			unsigned char *tmp;

			rbsize <<=1;

			tmp = realbuf;
			realbuf = heap_realloc(realbuf,rbsize);
			if (!realbuf) {
				realbuf = tmp;
				break;
			}
			spleft += rbsize >> 1;

			ptr = realbuf+ptrloc;

			dbgprintf(PR_WARN, "updated size now %ld, splef %ld, ptrloc %ld, ptr 0x%p, realbuf 0x%p\n",rbsize,spleft,ptrloc,ptr,realbuf);
		}
	}
	CloseHandle(hinpipe);

	bread = rbsize-spleft;

	hclipbuf = GlobalAlloc(GMEM_MOVEABLE|GMEM_DDESHARE, bread+256);
	if (!hclipbuf) {
		dbgprintf(PR_ERROR, "!!! %s(): GlobalAlloc() error %ld\n", __FUNCTION__, GetLastError());
		is_dev_clipboard_active = 0;
		return;
	}
	clipbuf = (unsigned char*)GlobalLock(hclipbuf);

	if (!clipbuf) {
		dbgprintf(PR_ERROR, "!!! %s(): GlobalLock() error %ld\n", __FUNCTION__, GetLastError());
		GlobalFree(hclipbuf);
		is_dev_clipboard_active = 0;
		return;
	}
	ptr = clipbuf;
	for (i=0; i <bread; i++) {

		if (realbuf[i] == '\n' && (i >0 && realbuf[i-1] != '\r'))
			*ptr++ = '\r';

		*ptr++ =realbuf[i];

		if ((ptr - clipbuf) >= (intptr_t)rbsize)
			break;
	}
	*ptr=0;

	heap_free(realbuf);

	GlobalUnlock(clipbuf);

	if (!OpenClipboard(ghwndmain))
		goto error;

	if (!EmptyClipboard())
		goto error;
		
	if (SetClipboardData(CF_TEXT,hclipbuf) != hclipbuf){
		err = GetLastError();
		goto error;

	}
	CloseClipboard();
	is_dev_clipboard_active = 0;
	return;

error:
	is_dev_clipboard_active = 0;
	GlobalFree(hclipbuf);
	CloseClipboard();
}

HANDLE create_clip_writer_thread(void)
{
	HANDLE  hread,hwrite;
	DWORD tid;
	SECURITY_ATTRIBUTES secd;

	if (is_dev_clipboard_active)
		return INVALID_HANDLE_VALUE;
	secd.nLength=sizeof(secd);
	secd.lpSecurityDescriptor=NULL;
	secd.bInheritHandle=FALSE;

	if (!CreatePipe(&hread, &hwrite, &secd,0)) {
		dbgprintf(PR_ERROR, "!!! %s(): CreatePipe() error %ld\n", __FUNCTION__, GetLastError());
                /* @@@@ abort ? */
		abort();
	}
	is_dev_clipboard_active = 1;
	ghdevclipthread = CreateThread(NULL, STKSIZE, (LPTHREAD_START_ROUTINE)clip_writer_proc, hread, 0, &tid);
//	CloseHandle(ht);
	return hwrite;
}

/* Read from clipboard and write to pipe */
void clip_reader_proc(HANDLE houtpipe) {

	HANDLE hclip;
	unsigned char *cbp;
	unsigned char *clipbuf;
	unsigned char * outbuf,*ptr;
	DWORD bwrote, len;
	DWORD obsize;

	obsize = 4096;
	outbuf = heap_alloc(obsize);
	ptr = outbuf;


	if (!IsClipboardFormatAvailable(CF_TEXT))
		goto done;
	
	if (!OpenClipboard(ghwndmain))
		goto done;

	len = 0;
	hclip = GetClipboardData(CF_TEXT);
	if (hclip) {
		clipbuf = (unsigned char*)GlobalLock(hclip);
		cbp = clipbuf;
		while(*cbp ) {
			*ptr++ = *cbp++;
			len++;
			if (len == obsize) {
				obsize <<= 1;
				outbuf = heap_realloc(outbuf,obsize);
				if (!outbuf)
					break;
				ptr = outbuf+len;
			}
		}
		GlobalUnlock(hclip);
	}
	CloseClipboard();

	if (!WriteFile(houtpipe, outbuf, len, &bwrote, NULL)) {
		dbgprintf(PR_ERROR, "!!! %s(): WriteFile(0x%p, ..): error %ld\n", __FUNCTION__, houtpipe, GetLastError());
	}
	CloseHandle(houtpipe);
	heap_free(outbuf);

done:
	is_dev_clipboard_active=0;
	return;
}

HANDLE create_clip_reader_thread(void)
{
	HANDLE  hread,hwrite;
	DWORD tid;
	SECURITY_ATTRIBUTES secd;

	if (is_dev_clipboard_active)
		return INVALID_HANDLE_VALUE;

	secd.nLength=sizeof(secd);
	secd.lpSecurityDescriptor=NULL;
	secd.bInheritHandle=FALSE;

	if (!CreatePipe(&hread,&hwrite,&secd,0)) {
		dbgprintf(PR_ERROR, "!!! %s(): CreatePipe() error %ld\n", __FUNCTION__, GetLastError());
                /* @@@@ abort ? */
		abort();
	}
	is_dev_clipboard_active = 1;
	ghdevclipthread = CreateThread(NULL, STKSIZE, (LPTHREAD_START_ROUTINE)clip_reader_proc, hwrite, 0, &tid);
	return hread;
}
