
/* dist: public */

#include <stdio.h>
#include "asss.h"

local Imodman     *mm;
local Icmdman     *cmd;
local Ichat       *chat;
local Iconfig     *cfg;

int LLSort_CommandInfo(const void *a_, const void *b_)
{
        const CommandInfo *a = a_;
        const CommandInfo *b = b_;

        return LLSort_StringCompare(a->name, b->name);
}

local helptext_t listcommand_help =
        "Targets: none, Player\n"
        "Displays all the commands that you (or the specified player) can use.\n"
        "Commands in the arena section are specific to the current arena.\n"
        "The symbol before the command specifies how you can use the command:\n"
        "A dot '.' means you use the command without sending it to a player, "
        "it might apply to the entire zone, the current arena or to yourself.\n"
        "A slash '/' means you can send the command in a private message to a player, "
        "the effects will then apply to that player only.\n"
        "A colon ':' means you can send the command in a private message to a player in a different arena"
        ;

local void DoList(Arena *arena, Player *p, Player *sendTo, int filterGlobal, int filterNoAccess)
{
        LinkedList *commands = cmd->GetCommands(arena, p, filterGlobal, filterNoAccess);
        Link *l;
        StringBuffer bufG;
        StringBuffer bufA;
        
        LLSort(commands, LLSort_CommandInfo);
        
        SBInit(&bufG);
        SBInit(&bufA);

        SBPrintf(&bufG, "Zone:");
        SBPrintf(&bufA, "Arena:");
        
        for (l = LLGetHead(commands); l; l = l->next)
        {
                CommandInfo *info = l->data;
                StringBuffer *buf = info->arena ? &bufA : &bufG;
                
                SBPrintf(buf, " ");
                
                SBPrintf(buf, "%s", info->can_arena || info->can_priv || info->can_rpriv ? "" : "!");
                SBPrintf(buf, "%s", info->can_arena ? "." : "");
                SBPrintf(buf, "%s", info->can_priv ? "/" : "");
                SBPrintf(buf, "%s", info->can_rpriv ? ":" : "");
                SBPrintf(buf, "%s", info->name);
        }
        
        chat->SendWrappedText(sendTo, SBText(&bufG, 0));
        chat->SendWrappedText(sendTo, SBText(&bufA, 0));
        
        SBDestroy(&bufG);
        SBDestroy(&bufA);
        cmd->FreeGetCommands(commands);
}

local void ListCommand(const char *command, const char *params, Player *p, const Target *target)
{
        if (target->type == T_PLAYER)
        {
                chat->SendMessage(p, "%s has access to the following commands:", target->u.p->name);
                DoList(p->arena,target->u.p, p, FALSE, TRUE);
        }
        else
        {
                chat->SendMessage(p, "You can use the following commands:");
                DoList(p->arena, p, p, FALSE, TRUE);

                const char *man = cfg->GetStr(GLOBAL, "Help", "CommandName");
                if (!man)
                {
                        man = "man";
                }

                chat->SendMessage(p, "Use ?%s <command name> to learn more about a command", man);
        }
}

local void ListAllCommand(const char *command, const char *params, Player *p, const Target *target)
{
        DoList(p->arena, target->type == T_PLAYER ? target->u.p : p, p, FALSE, FALSE);
}

EXPORT const char info_cmdlist[] = CORE_MOD_INFO("cmdlist");
local void ReleaseInterfaces()
{
        mm->ReleaseInterface(cmd );
        mm->ReleaseInterface(chat);
        mm->ReleaseInterface(cfg );
}

EXPORT int MM_cmdlist(int action, Imodman *mm_, Arena *arena)
{
        if (action == MM_LOAD)
        {
                mm = mm_;
                
                cmd     = mm->GetInterface(I_CMDMAN, ALLARENAS);
                chat    = mm->GetInterface(I_CHAT  , ALLARENAS);
                cfg     = mm->GetInterface(I_CONFIG, ALLARENAS);

                if (!cmd || !chat || !cfg)
                {
                        printf("<cmdlist> Missing Interface\n");

                        ReleaseInterfaces();
                        return MM_FAIL;
                }
                
                cmd->AddCommand("cmdlist", ListCommand, ALLARENAS, listcommand_help);
                cmd->AddCommand("commands", ListCommand, ALLARENAS, listcommand_help);
                cmd->AddCommand("allcommands", ListAllCommand, ALLARENAS, listcommand_help);
                
                return MM_OK;
        }
        else if (action == MM_UNLOAD)
        {
                cmd->RemoveCommand("cmdlist", ListCommand, ALLARENAS);
                cmd->RemoveCommand("commands", ListCommand, ALLARENAS);
                cmd->RemoveCommand("allcommands", ListAllCommand, ALLARENAS);
        
                ReleaseInterfaces();
                return MM_OK;
        }
        
        return MM_FAIL;
}
