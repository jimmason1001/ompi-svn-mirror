/**
 * VampirTrace
 * http://www.tu-dresden.de/zih/vampirtrace
 *
 * Copyright (c) 2005-2007, ZIH, TU Dresden, Federal Republic of Germany
 *
 * Copyright (c) 1998-2005, Forschungszentrum Juelich GmbH, Federal
 * Republic of Germany
 *
 * See the file COPYRIGHT in the package base directory for details
 **/

#include <string.h>
#include "vt_fbindings.h"
#include "vt_inttypes.h"
#include "vt_memhook.h"
#include "vt_pform.h"
#include "vt_trc.h"
#define VTRACE
#undef VTRACE_NO_COMMENT
#include "vt_user_comment.h"

static int vt_init = 1;        /* is initialization needed? */

#define VT_INIT \
  if ( vt_init ) { \
    VT_MEMHOOKS_OFF(); \
    vt_init = 0; \
    vt_open(); \
    VT_MEMHOOKS_ON(); \
  }

void VT_User_comment_def__(char* comment)
{
  VT_INIT;

  /* -- return, if tracing is disabled? -- */
  if ( !VT_IS_TRACE_ON() ) return;

  VT_MEMHOOKS_OFF();

  vt_def_comment(comment);

  VT_MEMHOOKS_ON();
}

void VT_User_comment__(char* comment)
{
  uint64_t time;

  VT_INIT;

  /* -- return, if tracing is disabled? -- */
  if ( !VT_IS_TRACE_ON() ) return;

  VT_MEMHOOKS_OFF();

  time = vt_pform_wtime();
  vt_comment(&time, comment);

  VT_MEMHOOKS_ON();
}

/*
 * Fortran version
 */

void VT_User_comment_def___f(char* comment, int cl);
void VT_User_comment___f(char* comment, int cl);

void VT_User_comment_def___f(char* comment, int cl)
{
  int comlen;
  char fcombuf[4096];

  /* -- convert Fortran to C strings -- */
  comlen = ( cl < 4096 ) ? cl : 4095;
  strncpy(fcombuf, comment, comlen);
  fcombuf[comlen] = '\0';

  VT_User_comment_def__(fcombuf);
} VT_GENERATE_F77_BINDINGS(vt_user_comment_def__, VT_USER_COMMENT_DEF__,
			   VT_User_comment_def___f,
			   (char* comment, int cl),
			   (comment, cl))

void VT_User_comment___f(char* comment, int cl)
{
  int comlen;
  char fcombuf[4096];

  /* -- convert Fortran to C strings -- */
  comlen = ( cl < 4096 ) ? cl : 4095;
  strncpy(fcombuf, comment, comlen);
  fcombuf[comlen] = '\0';

  VT_User_comment__(fcombuf);
} VT_GENERATE_F77_BINDINGS(vt_user_comment__, VT_USER_COMMENT__,
			   VT_User_comment___f,
			   (char* comment, int cl),
			   (comment, cl))
