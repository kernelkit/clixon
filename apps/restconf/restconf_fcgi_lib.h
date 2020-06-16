/*
 *
  ***** BEGIN LICENSE BLOCK *****
 
  Copyright (C) 2009-2019 Olof Hagsand
  Copyright (C) 2020 Olof Hagsand and Rubicon Communications, LLC(Netgate)

  This file is part of CLIXON.

  Licensed under the Apache License, Version 2.0 (the "License");
  you may not use this file except in compliance with the License.
  You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

  Unless required by applicable law or agreed to in writing, software
  distributed under the License is distributed on an "AS IS" BASIS,
  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  See the License for the specific language governing permissions and
  limitations under the License.

  Alternatively, the contents of this file may be used under the terms of
  the GNU General Public License Version 3 or later (the "GPL"),
  in which case the provisions of the GPL are applicable instead
  of those above. If you wish to allow use of your version of this file only
  under the terms of the GPL, and not to allow others to
  use your version of this file under the terms of Apache License version 2, 
  indicate your decision by deleting the provisions above and replace them with
  the  notice and other provisions required by the GPL. If you do not delete
  the provisions above, a recipient may use your version of this file under
  the terms of any one of the Apache License version 2 or the GPL.

  ***** END LICENSE BLOCK *****
  
 */

#ifndef _RESTCONF_FCGI_LIB_H_
#define _RESTCONF_FCGI_LIB_H_

/*
 * Prototypes
 */
int restconf_badrequest(clicon_handle h, FCGX_Request *r);
int restconf_unauthorized(clicon_handle h, FCGX_Request *r);
int restconf_forbidden(clicon_handle h, FCGX_Request *r);
int restconf_notfound(clicon_handle h, FCGX_Request *r);
int restconf_notacceptable(clicon_handle h, FCGX_Request *r);
int restconf_conflict(FCGX_Request *r);
int restconf_unsupported_media(FCGX_Request *r);
int restconf_internal_server_error(clicon_handle h, FCGX_Request *r);
int restconf_notimplemented(FCGX_Request *r);
int restconf_test(FCGX_Request *r,       int           dbg);
int clixon_restconf_params_set(clicon_handle h,
			   char        **envp);
int clixon_restconf_params_clear(clicon_handle h, char **envp);
cbuf *readdata(FCGX_Request *r);
int api_return_err(clicon_handle h, FCGX_Request *r, cxobj *xerr, int pretty, restconf_media media, int code0);
int http_location(clicon_handle h, FCGX_Request *r, cxobj *xobj);

#endif /* _RESTCONF_FCGI_LIB_H_ */