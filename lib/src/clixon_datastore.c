/*
 *
  ***** BEGIN LICENSE BLOCK *****
 
# Copyright (C) 2009-2016 Olof Hagsand and Benny Holmgren
# Copyright (C) 2017-2019 Olof Hagsand
# Copyright (C) 2020-2022 Olof Hagsand and Rubicon Communications, LLC(Netgate)

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

 * Clixon Datastore (XMLDB)
 * Saves Clixon data as clear-text XML (or JSON)
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
#include <dlfcn.h>
#include <fcntl.h>
#include <time.h>
#include <signal.h>
#include <libgen.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <sys/param.h>

/* cligen */
#include <cligen/cligen.h>

/* clixon */
#include "clixon_err.h"
#include "clixon_log.h"
#include "clixon_queue.h"
#include "clixon_hash.h"
#include "clixon_handle.h"
#include "clixon_string.h"
#include "clixon_file.h"
#include "clixon_yang.h"
#include "clixon_xml.h"
#include "clixon_yang_module.h"
#include "clixon_plugin.h"
#include "clixon_options.h"
#include "clixon_data.h"
#include "clixon_datastore.h"
#include "clixon_datastore_write.h"
#include "clixon_datastore_read.h"


/*! Translate from symbolic database name to actual filename in file-system
 * @param[in]   th       text handle handle
 * @param[in]   db       Symbolic database name, eg "candidate", "running"
 * @param[out]  filename Filename. Unallocate after use with free()
 * @retval      0        OK
 * @retval     -1        Error
 * @note Could need a way to extend which databases exists, eg to register new.
 * The currently allowed databases are: 
 *   candidate, tmp, running, result
 * The filename reside in CLICON_XMLDB_DIR option
 */
int
xmldb_db2file(clicon_handle  h, 
	      const char    *db,
	      char         **filename)
{
    int   retval = -1;
    cbuf *cb = NULL;
    char *dir;

    if ((cb = cbuf_new()) == NULL){
	clicon_err(OE_XML, errno, "cbuf_new");
	goto done;
    }
    if ((dir = clicon_xmldb_dir(h)) == NULL){
	clicon_err(OE_XML, errno, "dbdir not set");
	goto done;
    }
    cprintf(cb, "%s/%s_db", dir, db);
    if ((*filename = strdup4(cbuf_get(cb))) == NULL){
	clicon_err(OE_UNIX, errno, "strdup");
	goto done;
    }
    retval = 0;
 done:
    if (cb)
	cbuf_free(cb);
    return retval;
}

/*! Ensure database name is correct
 * @param[in]   db    Name of database 
 * @retval  0   OK
 * @retval  -1  Failed validate, xret set to error
 * XXX why is this function here? should be handled by netconf yang validation
 */
int
xmldb_validate_db(const char *db)
{
    if (strcmp(db, "running") != 0 && 
	strcmp(db, "candidate") != 0 && 
	strcmp(db, "startup") != 0 && 
	strcmp(db, "tmp") != 0)
	return -1;
    return 0;
}

/*! Connect to a datastore plugin, allocate resources to be used in API calls
 * @param[in]  h    Clicon handle
 * @retval     0    OK
 * @retval    -1    Error
 */
int
xmldb_connect(clicon_handle h)
{
    return 0;
}

/*! Disconnect from a datastore plugin and deallocate resources
 * @param[in]  handle  Disconect and deallocate from this handle
 * @retval     0       OK
 * @retval    -1    Error
 */
int
xmldb_disconnect(clicon_handle h)
{
    int       retval = -1;
    char    **keys = NULL;
    size_t    klen;
    int       i;
    db_elmnt *de;
    
    if (clicon_hash_keys(clicon_db_elmnt(h), &keys, &klen) < 0)
	goto done;
    for(i = 0; i < klen; i++) 
	if ((de = clicon_hash_value(clicon_db_elmnt(h), keys[i], NULL)) != NULL){
	    if (de->de_xml){
		xml_free(de->de_xml);
		de->de_xml = NULL;
	    }
	}
    retval = 0;
 done:
    if (keys)
	free(keys);
    return retval;
}

/*! Copy database from db1 to db2
 * @param[in]  h     Clicon handle
 * @param[in]  from  Source database
 * @param[in]  to    Destination database
 * @retval -1  Error
 * @retval  0  OK
  */
int 
xmldb_copy(clicon_handle h, 
	   const char   *from, 
	   const char   *to)
{
    int                 retval = -1;
    char               *fromfile = NULL;
    char               *tofile = NULL;
    db_elmnt           *de1 = NULL; /* from */
    db_elmnt           *de2 = NULL; /* to */
    db_elmnt            de0 = {0,};
    cxobj              *x1 = NULL;  /* from */
    cxobj              *x2 = NULL;  /* to */

    clicon_debug(1, "%s %s %s", __FUNCTION__, from, to);
    /* XXX lock */
    if (clicon_datastore_cache(h) != DATASTORE_NOCACHE){
	/* Copy in-memory cache */
	/* 1. "to" xml tree in x1 */
	if ((de1 = clicon_db_elmnt_get(h, from)) != NULL)
	    x1 = de1->de_xml;
	if ((de2 = clicon_db_elmnt_get(h, to)) != NULL)
	    x2 = de2->de_xml;
	if (x1 == NULL && x2 == NULL){
	    /* do nothing */
	}
	else if (x1 == NULL){  /* free x2 and set to NULL */
	    xml_free(x2);
	    x2 = NULL;
	}
	else  if (x2 == NULL){ /* create x2 and copy from x1 */
	    if ((x2 = xml_new(xml_name(x1), NULL, CX_ELMNT)) == NULL)
		goto done;
	    xml_flag_set(x2, XML_FLAG_TOP);
	    if (xml_copy(x1, x2) < 0) 
		goto done;
	}
	else{ /* copy x1 to x2 */
	    xml_free(x2);
	    if ((x2 = xml_new(xml_name(x1), NULL, CX_ELMNT)) == NULL)
		goto done;
	    xml_flag_set(x2, XML_FLAG_TOP);
	    if (xml_copy(x1, x2) < 0) 
		goto done;
	}
	/* always set cache although not strictly necessary in case 1
	 * above, but logic gets complicated due to differences with
	 * de and de->de_xml */
	if (de2)
	    de0 = *de2;
	de0.de_xml = x2; /* The new tree */
    }
    clicon_db_elmnt_set(h, to, &de0);

    /* Copy the files themselves (above only in-memory cache) */
    if (xmldb_db2file(h, from, &fromfile) < 0)
	goto done;
    if (xmldb_db2file(h, to, &tofile) < 0)
	goto done;
    if (clicon_file_copy(fromfile, tofile) < 0)
	goto done;
    retval = 0;
 done:
    if (fromfile)
	free(fromfile);
    if (tofile)
	free(tofile);
    return retval;
}

/*! Lock database
 * @param[in]  h    Clicon handle
 * @param[in]  db   Database
 * @param[in]  id   Session id
 * @retval -1  Error
 * @retval  0  OK
 */
int 
xmldb_lock(clicon_handle h, 
	   const char   *db, 
	   uint32_t      id)
{
    db_elmnt  *de = NULL;
    db_elmnt   de0 = {0,};

    if ((de = clicon_db_elmnt_get(h, db)) != NULL)
	de0 = *de;
    de0.de_id = id;
    clicon_db_elmnt_set(h, db, &de0);
    clicon_debug(1, "%s: locked by %u",  db, id);
    return 0;
}

/*! Unlock database
 * @param[in]  h   Clicon handle
 * @param[in]  db  Database
 * @retval -1  Error
 * @retval  0  OK
 * Assume all sanity checks have been made
 */
int 
xmldb_unlock(clicon_handle h, 
	     const char   *db)
{
    db_elmnt  *de = NULL;

    if ((de = clicon_db_elmnt_get(h, db)) != NULL){
	de->de_id = 0;
	clicon_db_elmnt_set(h, db, de);
    }
    return 0;
}

/*! Unlock all databases locked by session-id (eg process dies) 
 * @param[in]    h   Clicon handle
 * @param[in]    id  Session id
 * @retval -1    Error
 * @retval  0    OK
 */
int
xmldb_unlock_all(clicon_handle h, 
		 uint32_t      id)
{
    int       retval = -1;
    char    **keys = NULL;
    size_t    klen;
    int       i;
    db_elmnt *de;

    /* get all db:s */
    if (clicon_hash_keys(clicon_db_elmnt(h), &keys, &klen) < 0)
	goto done;
    /* Identify the ones locked by client id */
    for (i = 0; i < klen; i++) {
	if ((de = clicon_db_elmnt_get(h, keys[i])) != NULL &&
	    de->de_id == id){
	    de->de_id = 0;
	    clicon_db_elmnt_set(h, keys[i], de);
	}
    }
    retval = 0;
 done:
    if (keys)
	free(keys);
    return retval;
}

/*! Check if database is locked
 * @param[in] h   Clicon handle
 * @param[in] db  Database
 * @retval    -1  Error
 * @retval    0   Not locked
 * @retval    >0  Session id of locker
  */
uint32_t
xmldb_islocked(clicon_handle h, 
	       const char   *db)
{
    db_elmnt  *de;

    if ((de = clicon_db_elmnt_get(h, db)) == NULL)
	return 0;
    return de->de_id;
}

/*! Check if db exists or is empty
 * @param[in]  h   Clicon handle
 * @param[in]  db  Database
 * @retval -1  Error
 * @retval  0  No it does not exist
 * @retval  1  Yes it exists
 * @note  An empty datastore is treated as not existent so that a backend after dropping priviliges can re-create it
 */
int 
xmldb_exists(clicon_handle h, 
	     const char   *db)
{
    int                 retval = -1;
    char               *filename = NULL;
    struct stat         sb;

    clicon_debug(2, "%s %s", __FUNCTION__, db);
    if (xmldb_db2file(h, db, &filename) < 0)
	goto done;
    if (lstat(filename, &sb) < 0)
	retval = 0;

    else{
	if (sb.st_size == 0)
	    retval = 0;
	else
	    retval = 1;
    }
 done:
    if (filename)
	free(filename);
    return retval;
}

/*! Clear database cache if any for mem/size optimization only, not file itself
 * @param[in]  h   Clicon handle
 * @param[in]  db  Database
 * @retval -1  Error
 * @retval  0  OK
 */
int 
xmldb_clear(clicon_handle h, 
	    const char   *db)
{
    cxobj    *xt = NULL;
    db_elmnt *de = NULL;
    
    if ((de = clicon_db_elmnt_get(h, db)) != NULL){
	if ((xt = de->de_xml) != NULL){
	    xml_free(xt);
	    de->de_xml = NULL;
	}
    }
    return 0;
}

/*! Delete database, clear cache if any. Remove file 
 * @param[in]  h   Clicon handle
 * @param[in]  db  Database
 * @retval -1  Error
 * @retval  0  OK
 * @note  Datastore is not actually deleted so that a backend after dropping priviliges can re-create it
 */
int 
xmldb_delete(clicon_handle h, 
	     const char   *db)
{
    int                 retval = -1;
    char               *filename = NULL;
    struct stat         sb;
    
    clicon_debug(2, "%s %s", __FUNCTION__, db);
    if (xmldb_clear(h, db) < 0)
	goto done;
    if (xmldb_db2file(h, db, &filename) < 0)
	goto done;
    if (lstat(filename, &sb) == 0)
	if (truncate(filename, 0) < 0){
	    clicon_err(OE_DB, errno, "truncate %s", filename);
	    goto done;
	}
    retval = 0;
 done:
    if (filename)
	free(filename);
    return retval;
}

/*! Create a database. Open database for writing.
 * @param[in]  h   Clicon handle
 * @param[in]  db  Database
 * @retval  0  OK
 * @retval -1  Error
 */
int 
xmldb_create(clicon_handle h, 
	     const char   *db)
{
    int                 retval = -1;
    char               *filename = NULL;
    int                 fd = -1;
    db_elmnt           *de = NULL;
    cxobj              *xt = NULL;

    clicon_debug(2, "%s %s", __FUNCTION__, db);
    if ((de = clicon_db_elmnt_get(h, db)) != NULL){
	if ((xt = de->de_xml) != NULL){
	    xml_free(xt);
	    de->de_xml = NULL;
	}
    }
    if (xmldb_db2file(h, db, &filename) < 0)
	goto done;
    if ((fd = open(filename, O_CREAT|O_WRONLY, S_IRWXU)) == -1) {
	clicon_err(OE_UNIX, errno, "open(%s)", filename);
	goto done;
    }
   retval = 0;
 done:
    if (filename)
	free(filename);
    if (fd != -1)
	close(fd);
    return retval;
}

/*! Create an XML database. If it exists already, delete it before creating
 * Utility function.
 * @param[in]  h   Clixon handle
 * @param[in]  db  Symbolic database name, eg "candidate", "running"
 */
int
xmldb_db_reset(clicon_handle h, 
	       const char   *db)
{
    if (xmldb_exists(h, db) == 1){
	if (xmldb_delete(h, db) != 0 && errno != ENOENT) 
	    return -1;
    }
    if (xmldb_create(h, db) < 0)
	return -1;
    return 0;
}

/*! Get datastore XML cache
 * @param[in]  h    Clicon handle
 * @param[in]  db   Database name
 * @retval     xml  XML cached tree or NULL
 */
cxobj *
xmldb_cache_get(clicon_handle h,
		const char   *db)
{
    db_elmnt *de;
    
    if ((de = clicon_db_elmnt_get(h, db)) == NULL)
	return NULL;
    return de->de_xml;
}

/*! Get modified flag from datastore
 * @param[in]  h     Clicon handle
 * @param[in]  db    Database name
 * @retval    -1     Error (datastore does not exist)
 * @retval     0     Db is not modified
 * @retval     1     Db is modified
 * @note This only makes sense for "candidate", see RFC 6241 Sec 7.5
 * @note This only works if db cache is used,...
 */
int
xmldb_modified_get(clicon_handle h,
		   const char   *db)
{
    db_elmnt *de;
    
    if ((de = clicon_db_elmnt_get(h, db)) == NULL){
	clicon_err(OE_CFG, EFAULT, "datastore %s does not exist", db);
	return -1;
    }
    return de->de_modified;
}

/*! Get empty flag from datastore (the datastore was empty ON LOAD)
 * @param[in]  h     Clicon handle
 * @param[in]  db    Database name
 * @retval    -1     Error (datastore does not exist)
 * @retval     0     Db was not empty on load
 * @retval     1     Db was empty on load
 */
int
xmldb_empty_get(clicon_handle h,
		const char   *db)
{
    db_elmnt *de;
    
    if ((de = clicon_db_elmnt_get(h, db)) == NULL){
	clicon_err(OE_CFG, EFAULT, "datastore %s does not exist", db);
	return -1;
    }
    return de->de_empty;
}

/*! Set modified flag from datastore
 * @param[in]  h     Clicon handle
 * @param[in]  db    Database name
 * @param[in]  value 0 or 1
 * @retval    -1     Error (datastore does not exist)
 * @retval     0     OK
 * @note This only makes sense for "candidate", see RFC 6241 Sec 7.5
 * @note This only works if db cache is used,...
 */
int
xmldb_modified_set(clicon_handle h,
		   const char   *db,
		   int           value)
{
    db_elmnt *de;
    
    if ((de = clicon_db_elmnt_get(h, db)) == NULL){
	clicon_err(OE_CFG, EFAULT, "datastore %s does not exist", db);
	return -1;
    }
    de->de_modified = value;
    return 0;
}

/* Print the datastore meta-info to file
 */
int
xmldb_print(clicon_handle h,
	    FILE         *f)
{
    int       retval = -1;
    db_elmnt *de = NULL;    
    char    **keys = NULL;
    size_t    klen;
    int       i;

    if (clicon_hash_keys(clicon_db_elmnt(h), &keys, &klen) < 0)
	goto done;
    for (i = 0; i < klen; i++){
	/* XXX name */
	if ((de = clicon_db_elmnt_get(h, keys[i])) == NULL)
	    continue;
	fprintf(f, "Datastore:  %s\n", keys[i]);
	fprintf(f, "  Session:  %u\n", de->de_id);
	fprintf(f, "  XML:      %p\n", de->de_xml);
	fprintf(f, "  Modified: %d\n", de->de_modified);
	fprintf(f, "  Empty:    %d\n", de->de_empty);
    }
    retval = 0;
 done:
    return retval;
}

/*! Rename an XML database
 * @param[in]  h        Clicon handle
 * @param[in]  db       Database name
 * @param[in]  newdb    New Database name; if NULL, then same as old
 * @param[in]  suffix   Suffix to append to new database name
 * @retval    -1        Error
 * @retval     0        OK
 * @note if newdb and suffix are null, OK is returned as it is a no-op
 */
int
xmldb_rename(clicon_handle h,
             const char    *db,
             const char    *newdb,
             const char    *suffix)
{
    int    retval = -1;
    char  *old;
    char  *fname = NULL;
    cbuf  *cb = NULL;

    if ((xmldb_db2file(h, db, &old)) < 0)
        goto done;
    if (newdb == NULL && suffix == NULL)        // no-op
        goto done;
    if ((cb = cbuf_new()) == NULL){
	clicon_err(OE_XML, errno, "cbuf_new");
	goto done;
    }
    cprintf(cb, "%s", newdb == NULL ? old : newdb);
    if (suffix)
	cprintf(cb, "%s", suffix);
    fname = cbuf_get(cb);
    if ((rename(old, fname)) < 0) {
        clicon_err(OE_UNIX, errno, "rename: %s", strerror(errno));
        goto done;
    };
    retval = 0;
 done:
    if (cb)
	cbuf_free(cb);
    if (old)
        free(old);
    return retval;
}
