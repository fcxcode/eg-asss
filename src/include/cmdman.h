
/* dist: public */

#ifndef __CMDMAN_H
#define __CMDMAN_H

/** @file
 * the command manager; deals with registering and dispatching commands.
 *
 * command handlers come in two flavors, which differ only in whether
 * the handler gets to see the command name that was used. this can only
 * make a difference if the same handler is used for multiple commands,
 * of course. also, if you want to register per-arena commands, you need
 * to use the second flavor. all handlers may assume p and p->arena are
 * non-NULL.
 *
 * Target structs are used to describe how a command was issued.
 * commands typed as public chat messages get targets of type T_ARENA.
 * commands typed as local private messages or remove private messages
 * to another player on the same server get T_PLAYER targets, and
 * commands sent as team messages (to your own team or an enemy team)
 * get T_FREQ targets.
 *
 * there is no difference between ? commands and * commands. all
 * commands (except of course commands handled on the client) work
 * equally whether a ? or a * was used.
 *
 * help text should follow a standard format:
 * @code
 * local helptext_t help_foo =
 * "Module: ...\n"
 * "Targets: ...\n"
 * "Args: ...\n"
 * More text here...\n";
 * @endcode
 *
 * the server keeps track of a "default" command handler, which will get
 * called if no commands know to the server match a typed command. to
 * set or remove the default handler, pass NULL as cmdname to any of the
 * Add/RemoveCommand functions. this feature should only be used by
 * billing server modules.
 */


/** the type of command handlers.
 * @param command the name of the command that was issued
 * @param params the stuff that the player typed after the command name
 * @param p the player issuing the command
 * @param target describes how the command was issued (public, private,
 * etc.)
 */
typedef void (*CommandFunc)(const char *command, const char *params,
		Player *p, const Target *target);


/** a type representing the help text strings that get registered with
 ** the command manager. */
typedef const char *helptext_t;

typedef struct {
	const char *name;
	Arena *arena;
	helptext_t helptext;
	int can_arena;
	int can_priv;
	int can_rpriv;
} CommandInfo;

/** the interface id for Icmdman */
#define I_CMDMAN "cmdman-11"

/** the interface struct for Icmdman */
typedef struct Icmdman
{
	INTERFACE_HEAD_DECL

	/** Registers a command handler.
	 * Be sure to use RemoveCommand to unregister this before unloading.
	 * @param cmdname the name of the command being registered
	 * @param func the handler function
	 * @param ht some help text for this command, or NULL for none
	 */
	void (*AddCommand)(const char *cmdname, CommandFunc func, Arena *arena, helptext_t ht);

	/** Unregisters a command handler.
	 * Use this to unregister handlers registered with AddCommand.
	 */
	void (*RemoveCommand)(const char *cmdname, CommandFunc func, Arena *arena);

	/** Dispatches an incoming command.
	 * This is generally only called by the chat module and billing
	 * server modules.
	 * If the first character of typedline is a backslash, command
	 * handlers in the server will be bypassed and the command will be
	 * passed directly to the default handler.
	 * @param typedline the thing that the player typed
	 * @param p the player who issued the command
	 * @param target how the command was issued
	 * @param sound the sound from the chat packet that this command
	 * came from
	 */
	void (*Command)(const char *typedline, Player *p,
			const Target *target, int sound);
	helptext_t (*GetHelpText)(const char *cmdname, Arena *arena);

	/** Prevents a command's parameters from being logged.
	 * The command should be removed with RemoveUnlogged when the
	 * command is unregistered.
	 * @param cmdname the name of the command to not log
	 */
	void (*AddUnlogged)(const char *cmdname);

	/** Removes a command added with AddDontLog
	 * @param cmdname the name of the unlogged command to remove
	 */
	void (*RemoveUnlogged)(const char *cmdname);
	
	/** Get a list of registered commands.
	 * @param arena if set, also return commands registered to the given arena,
	 *              otherwise only return global commands.
	 * @param p if set the can_ fields are set using capman
	 * @param filterGlobal if TRUE, do not return any globally registered commands.
	 * @param filterNoAccess if TRUE, do not return any commands wherein all the can_ 
	 *                       are FALSE
	 * @return A linked list of CommandInfo structs, call FreeGetCommands() when you
	 *         are done.
	 */
	LinkedList* (*GetCommands)(Arena *arena, Player *p, int filterGlobal, int filterNoAccess);
	
	/** Free the return value of GetCommands()
	 * @param player if not null, only return commands that are valid
	 *               for the arena the player is in and also fill in the can_* attributes.
	 * @return A linked list of CommandInfo structs
	 */
	void (*FreeGetCommands)(LinkedList *list);
} Icmdman;

#endif

