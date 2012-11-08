wzsh
====

Native port of zsh 3.0.8 to Windows.


Build
-----

Compiled & working with::
  * Windows SDK 6.1, 7.0, 7.1
  * Visual Studio Express 2008 (command line cl) with Windows SDK 6.x, 7.x
  * Visual Studio Express 2010 (command line cl) with Windows SDK 7.x
      in 32 and 64 bit mode
  * Mingw32 gcc 3.4.5 && win32api 3.11, Mingw32 gcc 4.4.+ && win32api 3.14+
      Not tested with mingw gcc 4.6.2,
      see thread http://gcc.gnu.org/ml/gcc/2011-10/msg00253.html
      for `sejmp/longjmp` problems
  * Mingw64 TDM gcc 4.5.1 64 bit
  * gcc 4.x on Slackware 13.0 and 13.1
  * gcc 3.3 on Interix

Pre-requisites:
  * a GNU-make compatible make. mingw32-make from the mingw project is
      available at http://sourceforge.net/projects/mingw/files/MinGW/Extension/make/
      __nmake__ cannot parse the **Makefile**.
  * *sed* and *awk*, get them from the *Gnuwin32* project, and put *sed* and
      *gawk* in +PATH+.
  * a C compiler ;-). Add the path for the compiler and tools to Windows +PATH+.

The +configure+ step is not used for Windows, and a pre-built Win32/config.h
and Win32/Makefile are used. Before starting the compilation some environment
variables must be set, depending on the compiler:

    rem for mingw32
    set CC=gcc
    set RC=windres
    set C_ENV=MINGW32
  
    rem for mingw64
    set CC=gcc
    set RC=windres
    set C_ENV=MINGW64
  
    rem for Microsoft compiler 32 bits
    set CC=cl
    set RC=rc
    set C_ENV=MSC32
    rem

For Microsoft command line compilers add the path to the Windows SDK to the
environment variables LIB= and INCLUDE=.

Then, from the Src directory

    mingw32-make -f Win32/Makefile

For Microsoft Visual Studio Express 2008 and 2010 pre-defined project
files are in +Src/Win32/vsexpr-08+ and +Src/Win32/vsexpr-10+. These project do
not generate the needed .pro files, use the Win32/Makefile.


Use
---

Todo



Differences from the original zsh-nt
------------------------------------

User visible differences from the original zsh 3.0.5 port to win32, by Amol
Deshpande:

* _Removed support for Windows 95/98/Me and earlier_  
  Enforced at startup reading the Windows version. Some Win32 API used,
  like +GetFileAttributesEx()+, +SetFilePointerEx()+ are Version 5.0+ ONLY.
  NTFS filesystem required. Not tested on FAT.

* _No codepage muching_  
  Removed +ZSH_DONTSETCP+ environment variable and function.

* _Removed builtins_  
    * ps - Task Manager is always available
    * shutdown - There is a native *%SYSTEMROOT%\system32\shutdown.exe*
    * start - Use a new cmd.exe window

* _Removed support for non standard configuration files_  
  The standard files work just as well

* _Removed the environment variable *ZSHROOT*_  
  Added to the original zsh for Windows. Use a */bin* directory

* _Removed NT-specific setopts_  
    +winntnoassociations+:::
        Always try applications associated with extensions
    +winntwaitforguiapps+:::
        Always execute GUI apps asynchronously

Added  
  * Better support for +#!+ in shell scripts
  * Full +PATHEXT+ handling
  * Hack to support +/C+ or +/c+ instead of +-c+ on invocation and special
      parsing of the command line when +/C+ or +/c+ is used
  * Integrated configurable logger
  * Builtin test command understands Windows junctions
  * Many many many bug fixes

New upstream zsh 3.0.8  
  Well, not so new.  
  It brings some enhancements to zsh, like 64 bit arithmetic



Brief History
-------------

This port inherits from many other efforts. It was originally based on the
zsh 3.0.5 port by Amol Deshpande. Using and fixing this port I found a better
"fork" library in the win32 directory of the tcsh tree (http://www.tcsh.org).
After merging the new files I and ported the result to zsh 3.0.8. Still some
files in the zsh/ntport directory had a copyright statement with conditions
that were difficult to comply with. The WinZsh effort
(http://zsh-nt.sourceforge.net) obtained the permission to change the license
for some zsh-specific files to 3-clause BSD. One last step was to find a
replacement for the GPL licensed termcap.c, and it was found in ANSICON
(https://github.com/adoxa/ansicon).

Now all the files in the Win32 directory are unded 3 clause BSD or MIT-style
license.


Contributions
-------------

Todo

