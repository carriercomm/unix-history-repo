/****************************************************************************
 * Copyright (c) 1998 Free Software Foundation, Inc.                        *
 *                                                                          *
 * Permission is hereby granted, free of charge, to any person obtaining a  *
 * copy of this software and associated documentation files (the            *
 * "Software"), to deal in the Software without restriction, including      *
 * without limitation the rights to use, copy, modify, merge, publish,      *
 * distribute, distribute with modifications, sublicense, and/or sell       *
 * copies of the Software, and to permit persons to whom the Software is    *
 * furnished to do so, subject to the following conditions:                 *
 *                                                                          *
 * The above copyright notice and this permission notice shall be included  *
 * in all copies or substantial portions of the Software.                   *
 *                                                                          *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS  *
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF               *
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.   *
 * IN NO EVENT SHALL THE ABOVE COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,   *
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR    *
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR    *
 * THE USE OR OTHER DEALINGS IN THE SOFTWARE.                               *
 *                                                                          *
 * Except as contained in this notice, the name(s) of the above copyright   *
 * holders shall not be used in advertising or otherwise to promote the     *
 * sale, use or other dealings in this Software without prior written       *
 * authorization.                                                           *
 ****************************************************************************/

/****************************************************************************
 *   Author: Juergen Pfeifer <juergen.pfeifer@gmx.net> 1995,1997            *
 ****************************************************************************/

#include "form.priv.h"

MODULE_ID("$Id: frm_scale.c,v 1.4 1999/05/16 17:22:02 juergen Exp $")

/*---------------------------------------------------------------------------
|   Facility      :  libnform  
|   Function      :  int scale_form( const FORM *form, int *rows, int *cols )
|   
|   Description   :  Retrieve size of form
|
|   Return Values :  E_OK              - no error
|                    E_BAD_ARGUMENT    - invalid form pointer
|                    E_NOT_CONNECTED   - no fields connected to form
+--------------------------------------------------------------------------*/
int scale_form(const FORM * form, int * rows, int * cols)
{
  if ( !form )
    RETURN(E_BAD_ARGUMENT);

  if ( !(form->field) )
    RETURN(E_NOT_CONNECTED);
  
  if (rows) 
    *rows = form->rows;
  if (cols) 
    *cols = form->cols;
  
  RETURN(E_OK);
}

/* frm_scale.c ends here */
