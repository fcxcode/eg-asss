
/* dist: public */

#ifndef WIN32
#include <unistd.h>
#endif

#include <stdio.h>
#include "asss.h"


typedef struct TimerData
{
	TimerFunc func;
	ticks_t interval, when;
	void *param;
	void *key;
	int killme;
} TimerData;

typedef struct WorkData
{
	WorkFunc func;
	void *param;
} WorkData;

typedef struct RunInMainData
{
	RunInMainFunc func;
	void *param;

	int iswait;
	int waitresolved;
	pthread_cond_t wait_cond;
} RunInMainData;

local pthread_t main_thread;
local int privatequit;
local TimerData *thistimer;
local LinkedList timers;
local Imodman *mm;

local pthread_t worker_threads[CFG_THREAD_POOL_WORKER_THREADS];
local MPQueue work_queue;

local MPQueue runinmain_queue;
local pthread_mutex_t runinmain_wait_mtx = PTHREAD_MUTEX_INITIALIZER;

local pthread_mutex_t tmrmtx = PTHREAD_MUTEX_INITIALIZER;
#define LOCK() pthread_mutex_lock(&tmrmtx)
#define UNLOCK() pthread_mutex_unlock(&tmrmtx)

void handleRunInMain(RunInMainData *md)
{
	if (md->iswait)
	{
		pthread_mutex_lock(&runinmain_wait_mtx);
		md->waitresolved = 1;
		pthread_cond_signal(&md->wait_cond);
		pthread_mutex_unlock(&runinmain_wait_mtx);
		// other thread takes care of cleanup
	}
	else
	{
		md->func(md->param);
		afree(md);
	}
}

void drainRunInMain()
{
	RunInMainData *md;
	while ( (md = MPTryRemove(&runinmain_queue)) )
	{
		handleRunInMain(md);
	}
}

int RunLoop(void)
{
	TimerData *td;
	Link *l;
	ticks_t gtc;

	main_thread = pthread_self();

	set_thread_name(main_thread, "asss-main");

	while (!privatequit)
	{
		/* call all funcs */
		DO_CBS(CB_MAINLOOP, ALLARENAS, MainLoopFunc, ());

		drainRunInMain();

		/* do timers */
		LOCK();
startover:
		gtc = current_ticks();
		for (l = LLGetHead(&timers); l; l = l->next)
		{
			td = (TimerData*) l->data;
			if (td->func && TICK_GT(gtc, td->when))
			{
				int ret;
				thistimer = td;
				UNLOCK();
				ret = td->func(td->param);
				LOCK();
				thistimer = NULL;
				if (td->interval == 0 || td->killme || !ret)
				{
					LLRemove(&timers, td);
					afree(td);
				}
				else
					td->when = gtc + td->interval;
				goto startover;
			}
		}
		UNLOCK();

		drainRunInMain();

		/* rest a bit (10ms) */
		TimeoutSpec timeout = schedule_timeout(10);

		RunInMainData *md;
		do
		{
			/* keep sleeping until the timeout is over */
			md = MPTimeoutRemove(&runinmain_queue, timeout);
			if (md)
			{
				handleRunInMain(md);

				/* handle remaining functions as fast as possible */
				drainRunInMain();
			}
		} while(md); /* MPTimeoutRemove returns NULL if the timeout expired */
	}

	return privatequit & 0xff;
}


void KillML(int code)
{
	privatequit = 0x100 | (code & 0xff);
}


void StartTimer(TimerFunc f, int startint, int interval, void *param, void *key)
{
	TimerData *data = amalloc(sizeof(TimerData));

	data->func = f;
	data->interval = interval;
	data->when = TICK_MAKE(current_ticks() + startint);
	data->param = param;
	data->key = key;
	LOCK();
	LLAdd(&timers, data);
	UNLOCK();
}


void CleanupTimer(TimerFunc func, void *key, CleanupFunc cleanup)
{
	Link *l, *next;

	LOCK();
	for (l = LLGetHead(&timers); l; l = next)
	{
		TimerData *td = l->data;
		next = l->next;

		if (td->func == func && (td->key == key || key == NULL))
		{
			if (cleanup)
				cleanup(td->param);
			/* we might be inside the timer we're trying to remove. if
			 * so, mark it for the main loop to take care of. if not, do
			 * the removal now. */
			if (td == thistimer)
			{
				td->killme = TRUE;
			}
			else
			{
				LLRemove(&timers, td);
				afree(td);
			}
		}
	}
	UNLOCK();
}

void ClearTimer(TimerFunc f, void *key)
{
	CleanupTimer(f, key, NULL);
}


void RunInThread(WorkFunc func, void *param)
{
	WorkData *wd = amalloc(sizeof(*wd));
	wd->func = func;
	wd->param = param;
	MPAdd(&work_queue, wd);
}

local void *thread_main(void *dummy)
{
	for (;;)
	{
		WorkData *wd = MPRemove(&work_queue);
		if (!wd)
			return NULL;
		wd->func(wd->param);
		afree(wd);
	}
}

local void RunInMain(RunInMainFunc func, void *param)
{
	if (privatequit)
	{
		/* the main loop has quit */
		func(param);
		return;
	}

	RunInMainData *md = amalloc(sizeof(*md));
	md->func = func;
	md->param = param;
	md->iswait = 0;
	MPAdd(&runinmain_queue, md);
}

local void WaitRunInMainDrain()
{
	if (pthread_equal(main_thread, pthread_self()))
	{
		drainRunInMain();
	}
	else
	{
		RunInMainData *md = amalloc(sizeof(*md));
		md->iswait = 1;
		md->waitresolved = 0;
		pthread_cond_init(&md->wait_cond, NULL);

		pthread_cleanup_push((void(*)(void*)) pthread_mutex_unlock, (void*) &runinmain_wait_mtx);
		pthread_mutex_lock(&runinmain_wait_mtx);

		MPAdd(&runinmain_queue, md);

		while (!md->waitresolved)
		{
			pthread_cond_wait(&md->wait_cond, &runinmain_wait_mtx);
		}

		pthread_cleanup_pop(1);

		pthread_cond_destroy(&md->wait_cond);
		afree(md);
	}
}

local Imainloop _int =
{
	INTERFACE_HEAD_INIT(I_MAINLOOP, "mainloop")
	StartTimer, ClearTimer, CleanupTimer, RunLoop, KillML,
	RunInThread,
	RunInMain, WaitRunInMainDrain
};

EXPORT const char info_mainloop[] = CORE_MOD_INFO("mainloop");

EXPORT int MM_mainloop(int action, Imodman *mm_, Arena *arena)
{
	int i;

	if (action == MM_LOAD)
	{
		mm = mm_;
		privatequit = 0;
		LLInit(&timers);
		MPInit(&work_queue);
		MPInit(&runinmain_queue);
		for (i = 0; i < CFG_THREAD_POOL_WORKER_THREADS; i++)
		{
			pthread_create(&worker_threads[i], NULL, thread_main, NULL);
			set_thread_name(worker_threads[i], "asss-worker-%d", i);
		}
		mm->RegInterface(&_int, ALLARENAS);
		return MM_OK;
	}
	else if (action == MM_UNLOAD)
	{
		LLEnum(&timers, afree);
		LLEmpty(&timers);
		for (i = 0; i < CFG_THREAD_POOL_WORKER_THREADS; i++)
			MPAdd(&work_queue, NULL);
		for (i = 0; i < CFG_THREAD_POOL_WORKER_THREADS; i++)
			pthread_join(worker_threads[i], NULL);
		MPDestroy(&work_queue);
		MPDestroy(&runinmain_queue);
		if (mm->UnregInterface(&_int, ALLARENAS))
			return MM_FAIL;
		return MM_OK;
	}
	return MM_FAIL;
}

