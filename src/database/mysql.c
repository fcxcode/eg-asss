
/* dist: public */

#ifndef WIN32
#include <unistd.h>
/* #include <signal.h> */
#endif

#include <stdio.h>
#include <stdarg.h>
#include <string.h>

#include "asss.h"

#include "reldb.h"

#include "mysql.h"


struct db_cmd
{
	enum
	{
		CMD_NULL,
		CMD_QUERY,
	} type;
	query_callback cb;
	int cb_status;
	db_res *cb_res;
	void *clos;
	int qlen, flags;
#define FLAG_NOTIFYFAIL 0x01
	char query[1];
};


local Iconfig *cfg;
local Imainloop *ml;
local Ilogman *lm;

local const char *host, *user, *pw, *dbname;

local MPQueue dbq;
local pthread_t wthd;
local volatile int connected;

local MYSQL *mydb;

local void run_query_cb(struct db_cmd *cmd)
{
	if (cmd->cb && (cmd->cb_status == 0 || cmd->flags & FLAG_NOTIFYFAIL))
	{
		cmd->cb(cmd->cb_status, cmd->cb_res, cmd->clos);
	}

	if (cmd->cb_res)
	{
		mysql_free_result((MYSQL_RES *) cmd->cb_res);
	}

	afree(cmd);
}

local void do_query(struct db_cmd *cmd)
{
	int q;

	cmd->cb_status = 0;
	cmd->cb_res = NULL;

#ifdef CFG_LOG_MYSQL_QUERIES
	if (lm) lm->Log(L_DRIVEL, "<mysql> query: %s", cmd->query);
#endif

	q = mysql_real_query(mydb, cmd->query, cmd->qlen);

	if (q)
	{
		if (lm)
			lm->Log(L_WARN, "<mysql> error in query: %s", mysql_error(mydb));

		cmd->cb_status = mysql_errno(mydb);
		ml->RunInMain((RunInMainFunc) run_query_cb, cmd);
		return;
	}

	if (mysql_field_count(mydb) == 0)
	{
		/* this wasn't a select, we have no data to report */
		ml->RunInMain((RunInMainFunc) run_query_cb, cmd);
	}
	else
	{
		MYSQL_RES *res = mysql_store_result(mydb);

		if (res == NULL)
		{
			if (lm)
				lm->Log(L_WARN, "<mysql> error in store_result: %s", mysql_error(mydb));

			cmd->cb_status = mysql_errno(mydb);
			ml->RunInMain((RunInMainFunc) run_query_cb, cmd);
			return;
		}

		cmd->cb_res = (db_res*) res;
		ml->RunInMain((RunInMainFunc) run_query_cb, cmd);
	}
}


local void close_db(void *v)
{
	connected = 0;
	mysql_close((MYSQL*)v);
}

local void * work_thread(void *dummy)
{
	struct db_cmd *cmd;

	mydb = mysql_init(NULL);

	if (mydb == NULL)
	{
		if (lm) lm->Log(L_WARN, "<mysql> init failed: %s", mysql_error(mydb));
		return NULL;
	}

	pthread_cleanup_push(close_db, mydb);

	connected = 0;

	/* try to connect */
	if (lm)
		lm->Log(L_INFO, "<mysql> connecting to mysql db on %s, user %s, db %s", host, user, dbname);
	while (mysql_real_connect(mydb, host, user, pw, dbname, 0, NULL, CLIENT_COMPRESS) == NULL)
	{
		if (lm) lm->Log(L_WARN, "<mysql> connect failed: %s", mysql_error(mydb));
		pthread_testcancel();
		sleep(60);
		pthread_testcancel();
	}

	connected = 1;

	/* now serve requests */
	for (;;)
	{
		/* the pthread_cond_wait inside MPRemove is a cancellation point */
		cmd = MPRemove(&dbq);

		/* reconnect if necessary */
		if (mysql_ping(mydb))
		{
			if (mysql_real_connect(mydb, host, user, pw, dbname, 0, NULL, CLIENT_COMPRESS))
			{
				if (lm)
					lm->Log(L_INFO, "<mysql> Connection to database re-established.");
			}
			else
				if (lm) lm->Log(L_INFO, "<mysql> Attempt to re-establish database connection failed.");
		}

		switch (cmd->type)
		{
			case CMD_NULL:
				cmd->cb_status = 0;
				cmd->cb_res = NULL;
				ml->RunInMain((RunInMainFunc) run_query_cb, cmd);
				break;

			case CMD_QUERY:
				do_query(cmd);
				break;
		}
	}

	pthread_cleanup_pop(1);

	return NULL;
}


local int GetStatus()
{
	return connected;
}

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-but-set-variable"
local int Query(query_callback cb, void *clos, int notifyfail, const char *fmt, ...)
{
	va_list ap;
	const char *c;
	char *buf;
	int space = 0, dummy;
	struct db_cmd *cmd;

	va_start(ap, fmt);
	for (c = fmt; *c; c++)
		if (*c == '?')
			space += strlen(va_arg(ap, const char *)) * 2 + 3;
		else if (*c == '#')
		{
			dummy = va_arg(ap, unsigned int);
			space += 10;
		}
	va_end(ap);

	space += strlen(fmt);

	cmd = amalloc(sizeof(struct db_cmd) + space);
	cmd->type = CMD_QUERY;
	cmd->cb = cb;
	cmd->clos = clos;
	if (notifyfail)
		cmd->flags |= FLAG_NOTIFYFAIL;

	buf = cmd->query;

	va_start(ap, fmt);
	for (c = fmt; *c; c++)
	{
		if (*c == '?')
		{
			const char *str = va_arg(ap, const char *);
			*buf++ = '\'';
			/* don't use mysql_real_escape_string because the db might
			 * not be connected yet, and mysql crashes on that. */
			buf += mysql_escape_string(buf, str, strlen(str));
			*buf++ = '\'';
		}
		else if (*c == '#')
		{
			unsigned int arg = va_arg(ap, unsigned int);
			buf += sprintf(buf, "%u", arg);
		}
		else
			*buf++ = *c;
	}
	va_end(ap);

	*buf = 0;
	cmd->qlen = buf - cmd->query;

	MPAdd(&dbq, cmd);

	return 1;
}
#pragma GCC diagnostic pop


local int GetRowCount(db_res *res)
{
	return mysql_num_rows((MYSQL_RES*)res);
}

local int GetFieldCount(db_res *res)
{
	return mysql_num_fields((MYSQL_RES*)res);
}

local db_row * GetRow(db_res *res)
{
	return (db_row*)mysql_fetch_row((MYSQL_RES*)res);
}

local const char * GetField(db_row *row, int fieldnum)
{
	return ((MYSQL_ROW)row)[fieldnum];
}

local int GetLastInsertId(void)
{
	return mysql_insert_id(mydb);
}

local int EscapeString(const char *str, char *buf, int buflen)
{
	int n = strlen(str);
	/* this is the formula for the maximal blowup of the escaped string.
	 * if the buffer doesn't have enough room, fail. */
	if (n * 2 + 1 > buflen)
		return FALSE;
	mysql_escape_string(buf, str, n);
	return TRUE;
}



local Ireldb my_int =
{
	INTERFACE_HEAD_INIT(I_RELDB, "mysql-db")
	GetStatus,
	Query,
	GetRowCount, GetFieldCount,
	GetRow, GetField,
	GetLastInsertId,
	EscapeString
};

EXPORT const char info_mysql[] = CORE_MOD_INFO("mysql");

EXPORT int MM_mysql(int action, Imodman *mm, Arena *arena)
{
	/* static sighandler_t oldh; */

	if (action == MM_LOAD)
	{
		cfg = mm->GetInterface(I_CONFIG, ALLARENAS);
		ml = mm->GetInterface(I_MAINLOOP, ALLARENAS);
		lm = mm->GetInterface(I_LOGMAN, ALLARENAS);
		if (!cfg || !ml)
		{
			mm->ReleaseInterface(cfg);
			mm->ReleaseInterface(ml);
			mm->ReleaseInterface(lm);
			return MM_FAIL;
		}

		connected = 0;
		MPInit(&dbq);

		/* cfghelp: mysql:hostname, global, string, mod: mysql
		 * The name of the mysql server. */
		host = cfg->GetStr(GLOBAL, "mysql", "hostname");
		/* cfghelp: mysql:user, global, string, mod: mysql
		 * The mysql user to log in to the server as. */
		user = cfg->GetStr(GLOBAL, "mysql", "user");
		/* cfghelp: mysql:password, global, string, mod: mysql
		 * The password to log in to the mysql server as. */
		pw = cfg->GetStr(GLOBAL, "mysql", "password");
		/* cfghelp: mysql:database, global, string, mod: mysql
		 * The database on the mysql server to use. */
		dbname = cfg->GetStr(GLOBAL, "mysql", "database");

		if (!host || !user || !pw || !dbname)
		{
			mm->ReleaseInterface(cfg);
			mm->ReleaseInterface(ml);
			mm->ReleaseInterface(lm);
			return MM_FAIL;
		}
		host = astrdup(host);
		user = astrdup(user);
		pw = astrdup(pw);
		dbname = astrdup(dbname);

		/* oldh = signal(SIGPIPE, SIG_IGN); */

		pthread_create(&wthd, NULL, work_thread, NULL);
		set_thread_name(wthd, "asss-mysql");

		mm->RegInterface(&my_int, ALLARENAS);
		return MM_OK;
	}
	else if (action == MM_UNLOAD)
	{
		if (mm->UnregInterface(&my_int, ALLARENAS))
			return MM_FAIL;

		/* kill worker thread */
		pthread_cancel(wthd);
		pthread_join(wthd, NULL);

		ml->WaitRunInMainDrain();

		MPDestroy(&dbq);
		afree(host); afree(user); afree(pw); afree(dbname);

		mm->ReleaseInterface(cfg);
		mm->ReleaseInterface(ml);
		mm->ReleaseInterface(lm);

		/* signal(SIGPIPE, oldh); */

		return MM_OK;
	}
	return MM_FAIL;
}


