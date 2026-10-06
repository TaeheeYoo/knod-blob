/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * One .text for the whole blob, the hand-written routines first, then every
 * routine that has C in it as its glue, its C and the rest of its glue, in
 * that order - .text.<routine>, .text.<routine>.c, .text.<routine>.~ sort that
 * way by name.  See src/gda.inc.
 */
SECTIONS
{
	.text : {
		*(.text)
		*(SORT_BY_NAME(.text.*))
	}
}
