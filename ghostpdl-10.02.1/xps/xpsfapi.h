/* Copyright (C) 2001-2023 Artifex Software, Inc.
   All Rights Reserved.

   This software is provided AS-IS with no warranty, either express or
   implied.

   This software is distributed under license and may not be copied,
   modified or distributed except as expressly authorized under the terms
   of the license contained in the file LICENSE in this distribution.

   Refer to licensing information at http://www.artifex.com or contact
   Artifex Software, Inc.,  39 Mesa Street, Suite 108A, San Francisco,
   CA 94129, USA, for further information.
*/

/* Font API support  */

#ifndef xpsfapi_h_INCLUDED
#define xpsfapi_h_INCLUDED

int
xps_fapi_rebuildfont(gs_font *pfont);

int
xps_fapi_passfont(gs_font *pfont, char *fapi_request, char *file_name,
                  byte *font_data, int font_data_len);

#endif
