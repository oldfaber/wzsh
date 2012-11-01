/*
 * No copyright. This trivial file is offered as-is, without any warranty.
 */

/*
 * NOTE
 *      This file must be the _LAST_ object file linked in the application.
 *      The name starting with zz_ helps when is no other way to specify a
 *      link order
*/

/* marks end of memory to be copied to child */
unsigned long bookcommon2;
unsigned long bookbss2=0;
unsigned long bookdata2=1L;
