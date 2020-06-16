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
  * Generic restconf root handlers eg for /restconf /.well-known, etc
 */


#ifdef HAVE_CONFIG_H
#include "clixon_config.h" /* generated by config & autoconf */
#endif

#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <syslog.h>
#include <fcntl.h>
#include <time.h>
#include <limits.h>
#include <signal.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <libgen.h>
#include <sys/stat.h> /* chmod */

/* cligen */
#include <cligen/cligen.h>

/* clicon */
#include <clixon/clixon.h>

/* restconf */
#include "restconf_lib.h"
#include "restconf_api.h"
#include "restconf_root.h"


/*! Determine the root of the RESTCONF API
 * @param[in]  h        Clicon handle
 * @param[in]  req      Generic Www handle (can be part of clixon handle)
 * @param[in]  cb       Body buffer
 * @see RFC8040 3.1 and RFC7320
 * In line with the best practices defined by [RFC7320], RESTCONF
 * enables deployments to specify where the RESTCONF API is located.
 */
int
api_well_known(clicon_handle h,
	       void         *req)
{
    int       retval = -1;
    char     *request_method;
    cbuf     *cb = NULL;

    clicon_debug(1, "%s", __FUNCTION__);
    if (req == NULL){
	errno = EINVAL;
	goto done;
    }
    request_method = clixon_restconf_param_get(h, "REQUEST_METHOD");
    if (strcmp(request_method, "GET") != 0){
	restconf_method_notallowed(req, "GET");
	goto ok;
    }
    restconf_reply_status_code(req, 200); /* OK */
    restconf_reply_header_add(req, "Cache-Control", "no-cache");
    restconf_reply_header_add(req, "Content-Type", "application/xrd+xml");
    /* Create body */
    if ((cb = cbuf_new()) == NULL){
	clicon_err(OE_UNIX, errno, "cbuf_new");
	goto done;
    }
    cprintf(cb, "<XRD xmlns='http://docs.oasis-open.org/ns/xri/xrd-1.0'>\n");
    cprintf(cb, "   <Link rel='restconf' href='/restconf'/>\n");
    cprintf(cb, "</XRD>\r\n");

    /* Must be after body */
    restconf_reply_header_add(req, "Content-Length", "%d", cbuf_len(cb)); 
    if (restconf_reply_send(req, cb) < 0)
	goto done;
 ok:
    retval = 0;
 done:
    if (cb)
	cbuf_free(cb);
    return retval;
}
