/** signals.h                                 **/
/** architecture-customized signals.h for zsh **/

#define SIGCOUNT	22

#ifdef GLOBALS

char *sig_msg[SIGCOUNT+2] = {
	"done",
	"interrupt",
	"SIGBREAK",
	"hangup",
	"",
	"",
	"terminated",
	"killed",
	"illegal hardware instruction",
	"floating point exception",
	"alarm",
	"window size changed",
	"segmentation fault",
#ifdef USE_SUSPENDED
	"suspended (signal)",
#else
	"stopped (signal)",
#endif
	"broken pipe",
	"death of child",
	"continued",
	"",
#ifdef USE_SUSPENDED
	"suspended",
#else
	"stopped",
#endif
#ifdef USE_SUSPENDED
	"suspended (tty output)",
#else
	"stopped (tty output)",
#endif
#ifdef USE_SUSPENDED
	"suspended (tty input)",
#else
	"stopped (tty input)",
#endif
	"",
	"abort",
	NULL
};

char *sigs[SIGCOUNT+4] = {
	"EXIT",
	"INT",
	"BREAK",
	"HUP",
	"4",
	"5",
	"TERM",
	"KILL",
	"ILL",
	"FPE",
	"ALRM",
	"WINCH",
	"SEGV",
	"STOP",
	"PIPE",
	"CHLD",
	"CONT",
	"17",
	"TSTP",
	"TTOU",
	"TTIN",
	"21",
	"ABRT",
	"ZERR",
	"DEBUG",
	NULL
};

#else
extern char *sigs[SIGCOUNT+4],*sig_msg[SIGCOUNT+2];
#endif
#define sigmsg(sig) ((sig) <= SIGCOUNT ? sig_msg[sig] : "unknown signal")
